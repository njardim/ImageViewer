// ViewerWindow, part 1: the window, rendering, view state (zoom, pan, rotation, exposure). See
// viewer.h for the other parts.
#include "viewer.h"

#include <QElapsedTimer>
#include <QMenuBar>
#include <QTranslator>
#include <QScopeGuard>

#include "appearance.h"

#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QMimeData>
#include <QPlatformSurfaceEvent>
#include <QScreen>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

Q_LOGGING_CATEGORY(lcView, "imageviewer.view", QtWarningMsg)
// Off unless asked for, as the others: on, main.cpp ticks every 20 ms for as long as the viewer runs.
Q_LOGGING_CATEGORY(lcLatency, "imageviewer.latency", QtWarningMsg)

namespace {
constexpr int kDefaultMaxTexture = 16384;
constexpr double kMinZoom = 0.01;
constexpr double kMaxZoom = 64.0;
constexpr float kMaxExposureEv = 16.0f;
constexpr int kFolderSettleMs = 300; // changes on disk come in bursts
} // namespace

ViewerWindow::ViewerWindow(QVulkanInstance *vulkan)
    : m_renderer(this), m_settings(Settings::load()),
      // A bar never hides from under the pointer, nor while a menu is open (D-65).
      m_toolbarHide([this] {
          return QApplication::activePopupWidget() || slideshowBarAt(mapFromGlobal(QCursor::pos(screen()))) != -1;
      }),
      m_mainToolbarHide([this] {
          return QApplication::activePopupWidget() || m_mainToolbar.menuOpen()
                 || mainToolbarAt(mapFromGlobal(QCursor::pos(screen()))) != -1;
      }),
      m_galleryHide([this] {
          return QApplication::activePopupWidget() || m_gallery.contains(mapFromGlobal(QCursor::pos(screen())));
      }),
      m_overlayHide([this] {
          // Over the top overlay, or in its band where it shows when the pointer is at the top.
          const QPointF at = mapFromGlobal(QCursor::pos(screen()));
          return QApplication::activePopupWidget() || (at.y() >= 0 && at.y() < topActivationHeight() && at.x() >= 0 && at.x() < width());
      }),
      m_panelHide([this] {
          return QApplication::activePopupWidget() || m_histogramMenuOpen // its menu, drawn by us (D-117)
                 || infoPanelRect().contains(mapFromGlobal(QCursor::pos(screen())) * devicePixelRatio());
      }),
      m_details([](const VectorDetail &job, const std::atomic_bool *cancel) {
          return decodeVectorArea(job.path, job.scale, job.area, cancel);
      }),
      m_gallery(m_settings, m_files, [](const QString &path, int pixels, qint64 maxPixels) {
          // sRGB as on the clipboard; 8 bits, premultiplied in encoded values like every overlay texture.
          const Image image = decodeThumbnailImage(path, pixels, maxPixels);
          if (!image.isValid())
              return QImage();
          QImage thumbnail = srgbImage(image).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
          // The file's own size and colour space, for the gallery's tooltip (D-116).
          thumbnail.setText(QStringLiteral("Size"), QStringLiteral("%1 %2").arg(image.sourceWidth).arg(image.sourceHeight));
          thumbnail.setText(QStringLiteral("Color"), image.colour.description);
          return thumbnail;
      })
{
#if defined(Q_OS_WIN)
    Q_UNUSED(vulkan);
    setSurfaceType(QSurface::Direct3DSurface);
#elif defined(Q_OS_MACOS)
    Q_UNUSED(vulkan);
    setSurfaceType(QSurface::MetalSurface);
#elif defined(IMAGEVIEWER_VULKAN)
    if (vulkan) {
        setSurfaceType(QSurface::VulkanSurface);
        setVulkanInstance(vulkan);
    } else {
        setSurfaceType(QSurface::OpenGLSurface);
    }
#else
    Q_UNUSED(vulkan);
    setSurfaceType(QSurface::OpenGLSurface);
#endif
    setTitle(QStringLiteral("ImageViewer"));
    setMinimumSize(kMinWindowSize);
    m_renderer.setOutputPreference(m_settings.output);
    m_decodePool.setMaxThreadCount(1);
    connect(&m_details, &VectorDetails::drawn, this, &ViewerWindow::detailDrawn);
    connect(&m_details, &VectorDetails::failed, this, &ViewerWindow::detailFailed);
    connect(&m_details, &VectorDetails::cleared, this, [this] { m_renderer.clearDetail(); });
    m_redrawTimer.setSingleShot(true);
    m_redrawTimer.setInterval(kDetailSettleMs); // the window has stayed: draw the SVG again (D-106)
    connect(&m_redrawTimer, &QTimer::timeout, this, &ViewerWindow::scheduleWork);
    connect(&m_gallery, &Gallery::changed, this, &ViewerWindow::updateGallery);
    connect(&m_gallery, &Gallery::textureChanged, this, [this](const QImage &image) {
        m_renderer.setOverlay(Renderer::GalleryLayer, image);
        requestUpdate();
    });
    m_recent = loadRecentFiles();
    installApplicationMenu();
    m_noticeTimer.setSingleShot(true);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] {
        m_notice.clear();
        updateOverlay();
    });
    m_topOverlayTimer.setSingleShot(true);
    connect(&m_topOverlayTimer, &QTimer::timeout, this, [this] {
        m_pointerAtTop = false;
        requestUpdate();
    });
    m_pointerTimer.setSingleShot(true);
    connect(&m_pointerTimer, &QTimer::timeout, this, [this] {
        if (m_dragging || QApplication::activePopupWidget() || QGuiApplication::modalWindow())
            return; // never under a press, a menu or a dialog
        m_pointerHidden = true;
        updateCursor();
    });
    m_folderTimer.setSingleShot(true);
    m_folderTimer.setInterval(kFolderSettleMs);
    connect(&m_folderTimer, &QTimer::timeout, this, &ViewerWindow::refreshFolder);
    connect(&m_folderWatcher, &QFileSystemWatcher::directoryChanged, &m_folderTimer, qOverload<>(&QTimer::start));
    connect(&m_folderWatcher, &QFileSystemWatcher::fileChanged, &m_folderTimer, qOverload<>(&QTimer::start));
    connect(this, &QWindow::visibilityChanged, this, [this](QWindow::Visibility visibility) {
        qCDebug(lcView) << "visibility" << visibility; // tests: full screen without a window manager
        updateOverlay(); // the top overlay has its own setting for full screen
        requestUpdate();
    });
    connect(&m_watcher, &QFutureWatcher<Image>::finished, this, &ViewerWindow::decodeFinished);
    m_framePool.setMaxThreadCount(1);
    connect(&m_frameWatcher, &QFutureWatcher<FrameResult>::finished, this, &ViewerWindow::frameDecoded);
    m_frameTimer.setSingleShot(true);
    connect(&m_frameTimer, &QTimer::timeout, this, &ViewerWindow::frameTimeout);
    connect(&m_slideshow, &Slideshow::due, this, &ViewerWindow::slideshowDue);
    // What hides by itself gives its place to what is beside it (D-113): every place follows at the
    // next frame (placeOverlays()).
    connect(&m_toolbarHide, &AutoHide::hiddenChanged, this, [this](bool hidden) {
        if (hidden)
            setSlideshowBarHover(-1);
        updateSlideshowToolbar();
    });
    connect(&m_mainToolbarHide, &AutoHide::hiddenChanged, this, [this](bool hidden) {
        if (hidden) {
            setMainToolbarHover(-1);
            closeToolbarMenu();
        }
        updateMainToolbar();
        requestUpdate();
    });
    connect(&m_galleryHide, &AutoHide::hiddenChanged, this, [this](bool hidden) {
        if (hidden) {
            m_gallery.setHover(-1);
            hideTip(TipSource::Gallery);
        }
        requestUpdate();
    });
    connect(&m_overlayHide, &AutoHide::hiddenChanged, this, [this] { requestUpdate(); });
    connect(&m_panelHide, &AutoHide::hiddenChanged, this, [this] { updateOverlay(); });
    configureAutoHide();
    // The pointer set at once: a window without one shows its parent's (X11's root has its own),
    // and updateCursor() sets it only when the shape it wants changes.
    setCursor(Qt::ArrowCursor);
    m_tooltipTimer.setSingleShot(true);
    connect(&m_tooltipTimer, &QTimer::timeout, this, &ViewerWindow::showPendingTip);
    connect(&m_copyWatcher, &QFutureWatcher<QImage>::finished, this, &ViewerWindow::imageCopied);
    m_samplesPool.setMaxThreadCount(1);
    connect(&m_samplesWatcher, &QFutureWatcher<std::shared_ptr<const FileSamples>>::finished, this,
            &ViewerWindow::fileSamplesRead);
    connect(this, &QWindow::screenChanged, this, [this] {
        if (m_rendererReady)
            m_renderer.refreshOutput();
        updateOverlay();
        updateNavigationButtons();
    });
    // The macOS application menu quits without closing the window first.
    connect(qGuiApp, &QCoreApplication::aboutToQuit, this, &ViewerWindow::saveSession);
}

ViewerWindow::~ViewerWindow()
{
    // QWindow's own destructor still hides a window that was never closed (the macOS
    // application menu quits that way) and emits visibilityChanged, when this object's
    // members are already destroyed: none of the handlers below may run from here on.
    disconnect(this, nullptr, this, nullptr);
    // Every task of the window's pools ends here, before any member goes, whatever the order of
    // their declarations: thumbnails not started yet are not decoded, the running ones are
    // waited for, as is the decode, the copy, the next frame and a stopped animation's trim.
    m_gallery.stop();
    if (m_jobCancel) // a decode in a worker process ends at once, as the detail's does in stop()
        m_jobCancel->store(true, std::memory_order_relaxed);
    m_details.stop();
    m_watcher.waitForFinished();
    m_copyWatcher.waitForFinished();
    m_frameWatcher.waitForFinished();
    m_framePool.waitForDone();
    m_samplesWatcher.waitForFinished();
}

int ViewerWindow::textureLimit(const QString &path) const
{
    int limit = m_rendererReady ? std::min(kDefaultMaxTexture, m_renderer.maxTextureSize()) : kDefaultMaxTexture;
    if (m_textureCap > 0 && path == m_textureCapPath)
        limit = std::min(limit, m_textureCap);
    return limit;
}

bool ViewerWindow::event(QEvent *e)
{
    switch (e->type()) {
    case QEvent::UpdateRequest:
        render();
        return true;
    case QEvent::PlatformSurface:
        if (static_cast<QPlatformSurfaceEvent *>(e)->surfaceEventType()
            == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            m_renderer.releaseSwapChain();
        break;
    case QEvent::Close:
        saveSession();
        break;
    case QEvent::WindowStateChange: {
        const Qt::WindowStates before = static_cast<QWindowStateChangeEvent *>(e)->oldState();
        if (windowStates().testFlag(Qt::WindowMinimized)) {
            if (!before.testFlag(Qt::WindowMinimized)) // also minimized by the title bar, the taskbar, the Dock
                m_statesBeforeMinimize |= before & (Qt::WindowFullScreen | Qt::WindowMaximized);
        } else if (before.testFlag(Qt::WindowMinimized)) {
            // Once the platform has finished bringing the window back: never from inside its notice.
            QMetaObject::invokeMethod(this, &ViewerWindow::returnFromMinimized, Qt::QueuedConnection);
        }
        break;
    }
    case QEvent::Enter:
        pointerActive();
        break;
    case QEvent::WindowBlocked: // a modal dialog takes the mouse events that would show the pointer
        m_pointerTimer.stop();
        if (std::exchange(m_pointerHidden, false))
            updateCursor();
        break;
    case QEvent::Leave:
        m_pointerTimer.stop();
        if (std::exchange(m_pointerIn, false) && m_pickerOn)
            requestUpdate(); // the loupe shows nothing under the pointer
        if (m_picker.setHover(-1))
            updatePicker();
        setHoverZone(Zone::None);
        setSlideshowBarHover(-1);
        setMainToolbarHover(-1);
        if (m_mainToolbar.setMenuHover(-1))
            updateMainToolbar();
        m_gallery.setHover(-1);
        m_tooltipTimer.stop();
        hideTip(); // the pointer has left what the tooltip names
        if (std::exchange(m_histogramChoiceHover, false))
            updateOverlay();
        if (std::exchange(m_histogramMenuHover, -1) != -1)
            updateHistogramMenu();
        setPointerAtTop(false);
        break;
    case QEvent::WindowActivate:
        // The user may have toggled HDR or changed SDR brightness meanwhile.
        if (m_rendererReady) {
            m_renderer.refreshOutput();
            updateOverlay();
        }
        break;
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *drop = static_cast<QDropEvent *>(e);
        const QList<QUrl> urls = drop->mimeData()->urls();
        if (std::any_of(urls.cbegin(), urls.cend(), [](const QUrl &u) { return u.isLocalFile(); })) {
            drop->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::ContextMenu: { // Menu key, Shift+F10
        auto *menu = static_cast<QContextMenuEvent *>(e);
        showContextMenu(menu->reason() == QContextMenuEvent::Mouse ? menu->globalPos()
                                                                    : mapToGlobal(QPoint(width() / 2, height() / 2)));
        return true;
    }
    case QEvent::Drop: {
        auto *drop = static_cast<QDropEvent *>(e);
        for (const QUrl &url : drop->mimeData()->urls()) {
            if (url.isLocalFile()) {
                drop->acceptProposedAction();
                openFile(url.toLocalFile());
                return true;
            }
        }
        break;
    }
    case QEvent::NativeGesture: {
        auto *gesture = static_cast<QNativeGestureEvent *>(e);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            zoomAt(1.0 + gesture->value(), gesture->position() * devicePixelRatio());
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWindow::event(e);
}

void ViewerWindow::exposeEvent(QExposeEvent *)
{
    if (!isExposed())
        return;
    if (!m_rendererReady && !m_rendererFailed)
        initializeRenderer();
    resumeOnScreen();
    requestUpdate();
}

void ViewerWindow::initializeRenderer()
{
    QString error;
    if (!m_renderer.initialize(&error)) {
        m_rendererFailed = true;
        // Not from inside expose/paint (a nested event loop in the platform's paint callback):
        // report once control is back in the event loop, then exit with an error status.
        QMetaObject::invokeMethod(this, [error] {
            QMessageBox box(QMessageBox::NoIcon, QStringLiteral("ImageViewer"), error);
            setMessageIcon(box, QMessageBox::Critical); // ours, in the system's own box (D-86)
            box.exec();
            QCoreApplication::exit(1);
        }, Qt::QueuedConnection);
        return;
    }
    m_rendererReady = true;
    updateOverlay();
    scheduleWork(); // preloading waits for the renderer (the texture limit is known now)
}

void ViewerWindow::resizeEvent(QResizeEvent *)
{
    hideTip(); // what it named has moved
    windowChanged(); // the image glides from where it was on the screen, after full screen (D-114)
    if (windowStates() == Qt::WindowNoState)
        m_normalGeometry = geometry();
    // A fitting zoom follows the window: the image point at the centre stays there (a long page
    // read in Fit to Width keeps its place), then the pan is kept within the image.
    const QSizeF view = deviceSize();
    if (m_fit && !m_laidOutIn.isEmpty() && view != m_laidOutIn) {
        const double before = fitZoom(*m_fit, m_laidOutIn);
        if (before > 0.0)
            m_pan *= currentZoom() / before;
    }
    m_laidOutIn = view;
    clampPan();
    updateOverlay(); // the fit zoom shown in the overlay may have changed
    updateNavigationButtons();
    requestUpdate();
}

void ViewerWindow::moveEvent(QMoveEvent *)
{
    if (windowStates() == Qt::WindowNoState)
        m_normalGeometry = geometry();
}

QSizeF ViewerWindow::deviceSize() const
{
    // What the image is drawn into: at a fractional device pixel ratio the window's size times
    // the ratio is off by a fraction of a pixel, enough to turn 100 % into 99.9 %.
    const QSize surface = m_renderer.surfaceSize();
    return surface.isEmpty() ? QSizeF(size()) * devicePixelRatio() : QSizeF(surface);
}

QSizeF ViewerWindow::displayedImageSize() const
{
    const QSizeF s(m_image.width, m_image.height);
    return (m_quarterTurns % 2) ? s.transposed() : s;
}

double ViewerWindow::fitZoom(FitMode mode, QSizeF view) const
{
    if (view.isEmpty())
        view = deviceSize();
    const QSizeF image = displayedImageSize();
    if (image.isEmpty() || view.isEmpty())
        return 1.0;
    const double width = view.width() / image.width(), height = view.height() / image.height();
    // 100 % and "Enlarge small images" are an SVG's own size, not its texture's, which is drawn no
    // larger than the window (D-94).
    const double natural = naturalZoom();
    double zoom = 1.0;
    switch (mode) {
    case FitMode::Window: zoom = std::min(width, height); break;
    case FitMode::Width: zoom = width; break;
    case FitMode::Height: zoom = height; break;
    case FitMode::Fill: zoom = std::max(width, height); break;
    case FitMode::Actual: return natural;
    }
    if (!m_settings.enlargeSmallImages)
        zoom = std::min(zoom, natural);
    return std::clamp(zoom, kMinZoom * natural, kMaxZoom * natural);
}

double ViewerWindow::zoomFor(QSizeF fileSize) const
{
    // As showImage() and fitZoom() set it for a new file: the settings' fit, or the locked one; a
    // locked zoom of no fit carries over as it is. The view turns back to none.
    const std::optional<FitMode> mode = m_settings.lockZoom ? m_fit : std::optional<FitMode>(m_settings.fitMode);
    if (!mode)
        return m_zoom;
    const QSizeF view = deviceSize();
    if (fileSize.isEmpty() || view.isEmpty())
        return 1.0;
    const double width = view.width() / fileSize.width(), height = view.height() / fileSize.height();
    double zoom = 1.0;
    switch (*mode) {
    case FitMode::Window: zoom = std::min(width, height); break;
    case FitMode::Width: zoom = width; break;
    case FitMode::Height: zoom = height; break;
    case FitMode::Fill: zoom = std::max(width, height); break;
    case FitMode::Actual: return 1.0;
    }
    if (!m_settings.enlargeSmallImages)
        zoom = std::min(zoom, 1.0);
    return std::clamp(zoom, kMinZoom, kMaxZoom);
}

double ViewerWindow::currentZoom() const
{
    return m_fit ? fitZoom(*m_fit) : m_zoom * naturalZoom();
}

double ViewerWindow::sourceScale() const
{
    // An SVG's, exactly as drawn: its sides rounded up to whole texels give the scale of neither.
    if (m_image.vector && m_image.documentScale > 0.0)
        return 1.0 / m_image.documentScale;
    return m_image.sourceWidth > 0 && m_image.width > 0 ? double(m_image.sourceWidth) / m_image.width : 1.0;
}

double ViewerWindow::naturalZoom() const
{
    // An SVG's detail draws the document at any zoom (D-93). An image reduced for the GPU has no more
    // pixels than its texture's: enlarged, they would only pass for the image's own at 100 %.
    return m_image.vector ? sourceScale() : 1.0;
}

double ViewerWindow::shownZoom() const
{
    return currentZoom() / sourceScale(); // exactly 1 at an SVG's 100 %
}

QRectF ViewerWindow::imageRect() const
{
    const QSizeF size = displayedImageSize() * currentZoom();
    const QPointF centre = QPointF(deviceSize().width(), deviceSize().height()) / 2.0 + m_pan;
    QRectF r(centre - QPointF(size.width(), size.height()) / 2.0, size);
    // Whole-pixel origin: at 100 % every texel lands exactly on one device pixel. An SVG's is the
    // corner its document starts at, wherever the view turns and mirrors it (after mirroring, the
    // view turns clockwise: Renderer::recordFrame): its texture's last texels are rarely whole, and
    // at a whole zoom the document's pixels then land on the screen's (D-94).
    QPointF origin = r.topLeft();
    if (m_image.vector) {
        const int turns = ((m_quarterTurns % 4) + 4) % 4;
        const bool right = turns == 1 || (turns == 0 && m_mirrored) || (turns == 2 && !m_mirrored);
        const bool bottom = turns == 2 || (turns == 1 && m_mirrored) || (turns == 3 && !m_mirrored);
        origin = QPointF(right ? r.right() : r.left(), bottom ? r.bottom() : r.top());
    }
    r.translate(QPointF(std::round(origin.x()), std::round(origin.y())) - origin);
    return r;
}

void ViewerWindow::zoomAt(double factor, const QPointF &devicePos)
{
    const double from = currentZoom(), natural = naturalZoom();
    const double to = std::clamp(from * factor, kMinZoom * natural, kMaxZoom * natural);
    if (to == from || m_image.width == 0)
        return;
    const QPointF centre = QPointF(deviceSize().width(), deviceSize().height()) / 2.0 + m_pan;
    const QPointF newCentre = devicePos - (devicePos - centre) * (to / from);
    m_pan = newCentre - QPointF(deviceSize().width(), deviceSize().height()) / 2.0;
    // Kept over 100 %, so that a locked zoom (Z) shows an SVG drawn at another size at the same per cent.
    const double relative = to / natural;
    m_zoom = std::abs(relative - 1.0) < 1e-3 ? 1.0 : relative; // snap to exact 100 %
    m_fit.reset();
    clampPan();
    updateOverlay();
    requestUpdate();
}

void ViewerWindow::chooseFit(FitMode mode)
{
    // The mode chosen stays for the next images (D-65): it is the setting "Zoom of a new image".
    // A locked zoom (Z) carries the mode too, since it keeps whatever the image shows.
    if (mode != m_settings.fitMode)
        savePreference([mode](Settings &s) { s.fitMode = mode; });
    if (mode == FitMode::Actual && m_image.width > 0) {
        // 100 % about the window's centre, which keeps the point under it.
        m_pan *= fitZoom(FitMode::Actual) / currentZoom();
        m_fit = mode;
        clampPan();
        updateOverlay();
        requestUpdate();
        return;
    }
    setFit(mode);
}

void ViewerWindow::setFit(FitMode mode)
{
    m_fit = mode;
    resetPan();
    updateOverlay();
    requestUpdate();
}

void ViewerWindow::resetPan()
{
    // Centred, with equal margins on both sides even where they lie outside the window (the
    // owner's choice for Fit to Height, D-85); a width-fitted page, pushed past the edge and
    // clamped back, from its top.
    m_pan = {};
    if (m_fit == FitMode::Width)
        m_pan.setY(std::numeric_limits<double>::max());
    clampPan();
}

bool ViewerWindow::canPan() const
{
    const QSizeF image = displayedImageSize() * currentZoom(), view = deviceSize();
    return image.width() > view.width() + 0.5 || image.height() > view.height() + 0.5;
}

void ViewerWindow::clampPan()
{
    const QSizeF image = displayedImageSize() * currentZoom(), view = deviceSize();
    auto clampAxis = [](double pan, double content, double viewport) {
        if (content <= viewport)
            return 0.0;
        const double limit = (content - viewport) / 2.0;
        return std::clamp(pan, -limit, limit);
    };
    m_pan = QPointF(clampAxis(m_pan.x(), image.width(), view.width()),
                    clampAxis(m_pan.y(), image.height(), view.height()));
}

Renderer::Frame ViewerWindow::imageFrame() const
{
    Renderer::Frame frame;
    frame.exposure = std::exp2(m_exposureEv);
    frame.toneMap = m_settings.toneMap;
    frame.clipWarning = m_settings.clipWarning;
    frame.contentPeak = m_image.maxComponent;
    frame.contentLuminancePeak = m_image.maxLuminance;
    frame.absoluteLuminance = m_image.colour.isAbsolute();
    frame.background[0] = float(m_settings.background.redF());
    frame.background[1] = float(m_settings.background.greenF());
    frame.background[2] = float(m_settings.background.blueF());
    frame.checkerboard = m_settings.checkerboard && m_image.hasAlpha;
    // The second colour is the background made lighter (dark backgrounds) or darker; the color
    // picker's checks use it whether the checkerboard is on or not (D-115).
    const float luma = 0.2126f * frame.background[0] + 0.7152f * frame.background[1] + 0.0722f * frame.background[2];
    const float delta = luma < 0.5f ? 0.12f : -0.12f;
    for (int c = 0; c < 3; ++c)
        frame.checkerColour[c] = std::clamp(frame.background[c] + delta, 0.0f, 1.0f);
    return frame;
}

void ViewerWindow::render()
{
    if (!m_rendererReady)
        return;
    QElapsedTimer frameTime;
    if (lcLatency().isDebugEnabled())
        frameTime.start();
    const auto timeFrame = qScopeGuard([&frameTime] {
        if (frameTime.isValid() && frameTime.elapsed() > 100)
            qCDebug(lcLatency) << "a frame took" << frameTime.elapsed() << "ms";
    });
    updateDetail();
    // The toolbar's buttons follow every state they show (enabled, on, the slideshow's) at each
    // frame: a few names and checks, its texture drawn again only when one changed.
    updateMainToolbar();
    Renderer::Frame frame = imageFrame();
    if (m_image.width > 0) {
        frame.imageRect = glidedImageRect(imageRect()); // imageRect() itself after a glide (D-114)
        frame.quarterTurns = m_quarterTurns;
        frame.mirrored = m_mirrored;
        const double zoom = currentZoom();
        // Without interpolation from 200 % (D-P09), to see an image's pixels; never for an SVG, which
        // has none: it is interpolated until its detail arrives (the owner, D-94).
        frame.nearest = !m_image.vector && (zoom >= 2.0 || std::abs(zoom - 1.0) < 1e-6);
        // The detail while it is not smaller on screen than drawn (it has no mipmaps): zooming in,
        // it is enlarged until the next one arrives; zooming out, the image takes over.
        const VectorDetail &detail = m_details.shown();
        frame.detail = !detail.path.isEmpty() && detail.path == m_image.path && detail.scale <= shownZoom() * 1.0001;
        if (frame.checkerboard) { // cells of 8 logical pixels, whatever the zoom
            const double cell = 8.0 * devicePixelRatio();
            frame.checkerCells[0] = float(m_image.width * zoom / cell);
            frame.checkerCells[1] = float(m_image.height * zoom / cell);
        }
    }
    placeOverlays(&frame);
    if (m_renderer.render(frame) == Renderer::RenderResult::DeviceLost) {
        recoverFromDeviceLoss();
        return;
    }
    if (m_renderer.takeImageUploadFailure() && m_image.width > 0) {
        // The GPU refused this texture: decode the same file again, at the device limit if
        // it exceeded it (first image, decoded before the renderer existed), else at half size.
        const int longest = std::max(m_image.width, m_image.height);
        m_textureCapPath = m_image.path;
        const int deviceMax = m_renderer.maxTextureSize();
        m_textureCap = longest > deviceMax ? deviceMax : longest / 2;
        if (currentPath() != m_image.path) {
            // The user has moved on meanwhile: the cap applies when this file comes back.
        } else if (m_textureCap >= 512) {
            m_imageStale = true;
            scheduleWork();
            //: %1: a size in pixels.
            m_message = tr("Reducing the image to fit the GPU (at most %1 px)…").arg(QLocale().toString(m_textureCap));
        } else {
            // Keep the path, so this file counts as shown (with its error) and is not
            // decoded and uploaded again until the user comes back to it.
            Image refused;
            refused.path = m_image.path;
            refused.fileSize = m_image.fileSize;
            refused.modified = m_image.modified;
            refused.error = tr("The GPU did not accept the image.");
            m_message.clear();
            m_error = refused.error;
            stopAnimation();
            m_image = std::move(refused);
            m_shownLimit = textureLimit(m_image.path); // counts as shown at the capped size too
        }
        updateOverlay();
        return;
    }
    // The output is only known once the swapchain exists, and changes with the screen.
    if (m_renderer.output().description != m_overlayOutput)
        updateOverlay();
}

void ViewerWindow::recoverFromDeviceLoss()
{
    // Driver reset, update or GPU switch (D3D11 TDR): every GPU object is gone, including
    // the image, whose pixels only lived on the GPU. Rebuild and decode it again.
    qWarning("ImageViewer: graphics device lost; reinitialising the renderer");
    m_renderer.releaseResources();
    m_rendererReady = false;
    initializeRenderer();
    if (m_rendererReady) {
        m_topOverlayKey.clear(); // every overlay texture is gone too
        m_gallery.invalidate();
        m_panelKey.clear();
        m_messageKey.clear();
        m_slideshowBarTexture = 0;
        m_mainToolbarTexture = 0;
        m_pickerTexture = 0;
        m_histogramMenuOpen = false; // its texture is gone with the others
        m_toolbarMenuTexture = 0;
        updateOverlay();
        updateNavigationButtons();
        m_imageStale = true; // uploaded again from the cache, or decoded again
        scheduleWork();
    }
    requestUpdate();
}

void ViewerWindow::savePreference(const std::function<void(Settings &)> &change)
{
    change(m_settings);
    Settings stored = Settings::load();
    change(stored);
    stored.save();
}

void ViewerWindow::applySettings(const Settings &settings)
{
    const Settings previous = m_settings;
    m_settings = settings;
    m_settings.save();
    if (settings.language != previous.language) {
        applyLanguage(settings.language);
        translateApplicationMenu();
        // Descriptions are composed while decoding: decode again for the new language,
        // including a decode still running (its result is dropped when it arrives).
        ++m_decodeGeneration;
        m_cache.clear();
        if (!m_image.path.isEmpty())
            m_imageStale = true;
    }
    if (settings.fontSize != previous.fontSize)
        applyFontSize(settings.fontSize); // the panels follow below: their keys hold the font
    if (settings.pointerAutoHide != previous.pointerAutoHide || settings.autoHideMs != previous.autoHideMs)
        pointerActive();
    if (settings.slideshowSeconds != previous.slideshowSeconds)
        m_slideshow.setInterval(int(std::lround(settings.slideshowSeconds * 1000))); // a running slideshow takes it at once
    if (settings.autoHideMs != previous.autoHideMs || settings.slideshowToolbarAutoHide != previous.slideshowToolbarAutoHide
        || settings.toolbarAutoHide != previous.toolbarAutoHide || settings.galleryAutoHide != previous.galleryAutoHide
        || settings.overlayAutoHide != previous.overlayAutoHide || settings.panelAutoHide != previous.panelAutoHide)
        configureAutoHide(); // each shows at once, and hides again as now set
    updateMainToolbar();
    m_renderer.setOutputPreference(settings.output);
    // The shown image takes a new zoom mode at once, unless its zoom is locked or was set by hand.
    if (settings.fitMode != previous.fitMode && m_fit && !settings.lockZoom)
        setFit(settings.fitMode);
    if (settings.windowFit != WindowFit::Never
        && (settings.windowFit != previous.windowFit || settings.windowFitPercent != previous.windowFitPercent))
        matchWindowToImage();
    clampPan(); // "Enlarge small images" can change the fitting zoom
    if (settings.sortBy != previous.sortBy || settings.sortDescending != previous.sortDescending)
        relist();
    else
        scheduleWork(); // preloading switched on or off, looping changed the neighbours
    setHoverZone(Zone::None);
    updateOverlay();
    requestUpdate();
}

void ViewerWindow::rotate(int quarterTurns)
{
    m_quarterTurns = ((m_quarterTurns + quarterTurns) % 4 + 4) % 4;
    clampPan();
    updateOverlay();
}

void ViewerWindow::flipHorizontal()
{
    // The renderer mirrors the source before rotating it; after an odd number of quarter
    // turns that would flip the screen vertically. H . R(t) = R(-t) . H keeps it horizontal.
    m_mirrored = !m_mirrored;
    if (m_quarterTurns % 2)
        m_quarterTurns = (m_quarterTurns + 2) % 4;
    updateOverlay(); // the panel's View row names the transform
}

void ViewerWindow::flipVertical()
{
    // On screen, V = R(180) . H.
    flipHorizontal();
    rotate(2);
}

void ViewerWindow::adjustExposure(float ev)
{
    m_exposureEv = std::clamp(m_exposureEv + ev, -kMaxExposureEv, kMaxExposureEv);
    updateOverlay();
}

void ViewerWindow::resetExposure()
{
    m_exposureEv = 0.0f;
    updateOverlay();
}

void ViewerWindow::toggleToneMap()
{
    savePreference([on = !m_settings.toneMap](Settings &s) { s.toneMap = on; });
    updateOverlay();
}

void ViewerWindow::toggleClipWarning()
{
    savePreference([on = !m_settings.clipWarning](Settings &s) { s.clipWarning = on; });
    updateOverlay();
}

void ViewerWindow::toggleInfo()
{
    savePreference([on = !m_settings.showInfo](Settings &s) { s.showInfo = on; });
    updateOverlay();
}
