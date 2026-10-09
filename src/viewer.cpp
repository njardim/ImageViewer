// ViewerWindow, part 1: the window, rendering, view state (zoom, pan, rotation, exposure)
// and input. See viewer.h for the other parts.
#include "viewer.h"

#include <QApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPlatformSurfaceEvent>
#include <QScreen>
#include <QStyleHints>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr int kDefaultMaxTexture = 16384;
constexpr double kMinZoom = 0.01;
constexpr double kMaxZoom = 64.0;
constexpr QSize kMinWindowSize(320, 240); // logical pixels; also when the window takes an image's size
constexpr float kMaxExposureEv = 16.0f;
constexpr int kFolderSettleMs = 300; // changes on disk come in bursts
} // namespace

ViewerWindow::ViewerWindow(QVulkanInstance *vulkan) : m_renderer(this), m_settings(Settings::load())
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
    m_recent = loadRecentFiles();
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
    connect(this, &QWindow::visibilityChanged, this, [this] {
        updateOverlay(); // the top overlay has its own setting for full screen
        requestUpdate();
    });
    connect(&m_watcher, &QFutureWatcher<Image>::finished, this, &ViewerWindow::decodeFinished);
    m_framePool.setMaxThreadCount(1);
    connect(&m_frameWatcher, &QFutureWatcher<FrameResult>::finished, this, &ViewerWindow::frameDecoded);
    m_frameTimer.setSingleShot(true);
    connect(&m_frameTimer, &QTimer::timeout, this, &ViewerWindow::frameTimeout);
    connect(&m_slideshowTimer, &QTimer::timeout, this, &ViewerWindow::slideshowTimeout);
    connect(&m_copyWatcher, &QFutureWatcher<QImage>::finished, this, &ViewerWindow::imageCopied);
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
    m_watcher.waitForFinished();
    m_copyWatcher.waitForFinished();
    m_frameWatcher.waitForFinished();
}

void ViewerWindow::showRestored(const SessionState &session)
{
    m_lastDirectory = session.lastDirectory;
    QRect geometry = session.geometry;
    bool placed = false;
    if (m_settings.rememberGeometry && geometry.isValid()) {
        // Only where a screen still shows a usable part of it (monitors change between
        // sessions), and never larger than that screen (a hand-edited or corrupt value).
        const QScreen *best = nullptr;
        qint64 bestArea = 0;
        for (const QScreen *screen : QGuiApplication::screens()) {
            const QRect visible = screen->availableGeometry().intersected(geometry);
            const qint64 area = qint64(visible.width()) * visible.height();
            if (visible.width() >= 160 && visible.height() >= 120 && area > bestArea) {
                best = screen;
                bestArea = area;
            }
        }
        if (best) {
            const QRect available = best->availableGeometry();
            geometry.setSize(geometry.size().boundedTo(available.size()));
            geometry.moveLeft(std::clamp(geometry.left(), available.left(), available.right() - geometry.width() + 1));
            geometry.moveTop(std::clamp(geometry.top(), available.top(), available.bottom() - geometry.height() + 1));
            placed = true;
        }
    }
    if (placed)
        setGeometry(geometry);
    else
        resize(1280, 800);
    m_normalGeometry = this->geometry();
    m_maximizedBeforeFullScreen = session.maximized;
    if (placed && session.fullScreen)
        showFullScreen();
    else if (placed && session.maximized)
        showMaximized();
    else
        show();
}

void ViewerWindow::saveSession() const
{
    SessionState session;
    session.geometry = m_normalGeometry.isValid() ? m_normalGeometry : geometry();
    session.fullScreen = windowStates().testFlag(Qt::WindowFullScreen);
    session.maximized = session.fullScreen ? m_maximizedBeforeFullScreen : windowStates().testFlag(Qt::WindowMaximized);
    session.lastFile = m_image.path;
    session.lastDirectory = m_lastDirectory;
    session.save(); // preferences are saved when they change, never here (U2)
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
        setHoverZone(Zone::None);
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
    if (m_waitingForExpose) { // an animation waited while the window was hidden
        m_waitingForExpose = false;
        frameTimeout();
    }
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
            QMessageBox::critical(nullptr, QStringLiteral("ImageViewer"), error);
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
    double zoom = 1.0;
    switch (mode) {
    case FitMode::Window: zoom = std::min(width, height); break;
    case FitMode::Width: zoom = width; break;
    case FitMode::Height: zoom = height; break;
    case FitMode::Fill: zoom = std::max(width, height); break;
    }
    if (!m_settings.enlargeSmallImages)
        zoom = std::min(zoom, 1.0);
    return std::clamp(zoom, kMinZoom, kMaxZoom);
}

double ViewerWindow::currentZoom() const
{
    return m_fit ? fitZoom(*m_fit) : m_zoom;
}

double ViewerWindow::shownZoom() const
{
    return m_image.sourceWidth > 0 ? currentZoom() * m_image.width / m_image.sourceWidth : currentZoom();
}

QRectF ViewerWindow::imageRect() const
{
    const QSizeF size = displayedImageSize() * currentZoom();
    const QPointF centre = QPointF(deviceSize().width(), deviceSize().height()) / 2.0 + m_pan;
    QRectF r(centre - QPointF(size.width(), size.height()) / 2.0, size);
    // Whole-pixel origin: at 100 % every texel lands exactly on one device pixel.
    r.moveTopLeft(QPointF(std::round(r.left()), std::round(r.top())));
    return r;
}

void ViewerWindow::zoomAt(double factor, const QPointF &devicePos)
{
    const double from = currentZoom();
    const double to = std::clamp(from * factor, kMinZoom, kMaxZoom);
    if (to == from || m_image.width == 0)
        return;
    const QPointF centre = QPointF(deviceSize().width(), deviceSize().height()) / 2.0 + m_pan;
    const QPointF newCentre = devicePos - (devicePos - centre) * (to / from);
    m_pan = newCentre - QPointF(deviceSize().width(), deviceSize().height()) / 2.0;
    m_zoom = std::abs(to - 1.0) < 1e-3 ? 1.0 : to; // snap to exact 100 %
    m_fit.reset();
    clampPan();
    updateOverlay();
    requestUpdate();
}

void ViewerWindow::setActualSize()
{
    if (m_image.width == 0)
        return;
    // Explicit, so that an image that already fits leaves fit mode too (it must stay at
    // 100 % when the window shrinks, and become pannable).
    m_pan /= currentZoom();
    m_zoom = 1.0;
    m_fit.reset();
    clampPan();
    updateOverlay();
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
    // Pushed past the edge and clamped back: the image's top, or its start in reading order.
    m_pan = {};
    if (m_fit == FitMode::Width)
        m_pan.setY(std::numeric_limits<double>::max());
    else if (m_fit == FitMode::Height)
        m_pan.setX(QGuiApplication::layoutDirection() == Qt::RightToLeft ? std::numeric_limits<double>::lowest()
                                                                          : std::numeric_limits<double>::max());
    clampPan();
}

bool ViewerWindow::canPan() const
{
    const QSizeF image = displayedImageSize() * currentZoom(), view = deviceSize();
    return image.width() > view.width() + 0.5 || image.height() > view.height() + 0.5;
}

void ViewerWindow::matchWindowToImage()
{
    QScreen *display = screen();
    if (!display || m_image.width == 0 || (windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen)))
        return;
    // The image at 100 %, within the chosen share of the screen's free area (frame included).
    const QRect available = display->availableGeometry();
    const QMargins frame = frameMargins();
    const QSizeF room = QSizeF(available.width() - frame.left() - frame.right(),
                               available.height() - frame.top() - frame.bottom())
                        * (m_settings.windowFitPercent / 100.0);
    QSizeF size = displayedImageSize() / devicePixelRatio();
    size *= std::min({1.0, room.width() / size.width(), room.height() / size.height()});
    // Rounded up: at a fractional device pixel ratio the window must hold every image pixel.
    const QSize logical(int(std::ceil(size.width() - 1e-6)), int(std::ceil(size.height() - 1e-6)));
    QRect target(QPoint(), logical.expandedTo(kMinWindowSize).boundedTo(available.size()));
    // Around the window's centre, moved back onto the screen where it would leave it.
    target.moveCenter(geometry().center());
    target.moveLeft(std::clamp(target.left(), available.left() + frame.left(),
                               std::max(available.left() + frame.left(), available.right() - frame.right() - target.width() + 1)));
    target.moveTop(std::clamp(target.top(), available.top() + frame.top(),
                              std::max(available.top() + frame.top(), available.bottom() - frame.bottom() - target.height() + 1)));
    setGeometry(target);
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
    if (frame.checkerboard) {
        // The second colour is the background made lighter (dark backgrounds) or darker.
        const float luma = 0.2126f * frame.background[0] + 0.7152f * frame.background[1] + 0.0722f * frame.background[2];
        const float delta = luma < 0.5f ? 0.12f : -0.12f;
        for (int c = 0; c < 3; ++c)
            frame.checkerColour[c] = std::clamp(frame.background[c] + delta, 0.0f, 1.0f);
    }
    return frame;
}

void ViewerWindow::render()
{
    if (!m_rendererReady)
        return;
    Renderer::Frame frame = imageFrame();
    if (m_image.width > 0) {
        frame.imageRect = imageRect();
        frame.quarterTurns = m_quarterTurns;
        frame.mirrored = m_mirrored;
        const double zoom = currentZoom();
        frame.nearest = zoom >= 2.0 || std::abs(zoom - 1.0) < 1e-6; // decision D-P09
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
            m_message = refused.error;
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
        m_panelKey.clear();
        updateOverlay();
        updateNavigationButtons();
        m_imageStale = true; // uploaded again from the cache, or decoded again
        scheduleWork();
    }
    requestUpdate();
}

double ViewerWindow::sideZoneWidth() const
{
    if (!m_settings.sideZones || m_files.size() < 2)
        return 0.0;
    // Narrow windows keep a middle for panning and double-clicks.
    return std::min<double>(m_settings.sideZoneWidth, width() / 4.0);
}

bool ViewerWindow::inSideStrip(const QPointF &position) const
{
    const double zone = sideZoneWidth();
    return zone > 0.0 && position.y() >= 0 && position.y() < height()
           && ((position.x() >= 0 && position.x() < zone) || (position.x() < width() && position.x() >= width() - zone));
}

ViewerWindow::Zone ViewerWindow::zoneAt(const QPointF &position) const
{
    if (!inSideStrip(position))
        return Zone::None;
    if (position.x() < sideZoneWidth())
        return hasNeighbour(-1) ? Zone::Previous : Zone::None;
    return hasNeighbour(+1) ? Zone::Next : Zone::None;
}

void ViewerWindow::setHoverZone(Zone zone)
{
    if (zone == m_hoverZone)
        return;
    m_hoverZone = zone;
    updateCursor();
    updateNavigationButtons();
}

void ViewerWindow::pointerActive()
{
    if (m_pointerHidden) {
        m_pointerHidden = false;
        updateCursor();
    }
    if (m_settings.pointerHideMs > 0)
        m_pointerTimer.start(m_settings.pointerHideMs);
    else
        m_pointerTimer.stop();
}

void ViewerWindow::updateCursor()
{
    setCursor(m_pointerHidden ? Qt::BlankCursor : m_hoverZone == Zone::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
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
        // Descriptions are composed while decoding: decode again for the new language,
        // including a decode still running (its result is dropped when it arrives).
        ++m_decodeGeneration;
        m_cache.clear();
        if (!m_image.path.isEmpty())
            m_imageStale = true;
    }
    if (settings.pointerHideMs != previous.pointerHideMs)
        pointerActive();
    if (m_slideshow && settings.slideshowSeconds != previous.slideshowSeconds)
        m_slideshowTimer.start(int(std::lround(settings.slideshowSeconds * 1000))); // a running slideshow takes the new interval
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

void ViewerWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && e->modifiers() == Qt::NoModifier && (m_slideshow || visibility() == QWindow::FullScreen)) {
        if (m_slideshow) {
            stopSlideshow();
            showNotice(tr("Slideshow stopped"));
        }
        if (visibility() == QWindow::FullScreen)
            leaveFullScreen();
        return;
    }
    if (!executeShortcut(e))
        QWindow::keyPressEvent(e);
}

void ViewerWindow::mousePressEvent(QMouseEvent *e)
{
    pointerActive();
    switch (e->button()) {
    case Qt::RightButton:
        showContextMenu(e->globalPosition().toPoint());
        return;
    case Qt::BackButton: // mouse side buttons
        step(-1);
        return;
    case Qt::ForwardButton:
        step(+1);
        return;
    case Qt::LeftButton:
        break;
    default:
        return;
    }
    m_pressZone = zoneAt(e->position());
    m_pressMoved = false;
    m_dragOrigin = e->position();
    m_panOrigin = m_pan;
    m_dragging = canPan();
    if (m_pressZone != Zone::None)
        updateNavigationButtons(); // pressed look
}

void ViewerWindow::mouseMoveEvent(QMouseEvent *e)
{
    pointerActive();
    if (!(e->buttons() & Qt::LeftButton)) {
        // No press in progress (or its release went elsewhere, e.g. to the context menu).
        m_dragging = false;
        m_pressZone = Zone::None;
        setHoverZone(zoneAt(e->position()));
        setPointerAtTop(e->position().y() >= 0 && e->position().y() < topActivationHeight());
        return;
    }
    if (m_pressZone != Zone::None && !m_pressMoved) {
        // A press on a side zone is a click until it moves beyond the platform's drag distance.
        if ((e->position() - m_dragOrigin).manhattanLength() < QGuiApplication::styleHints()->startDragDistance())
            return;
        m_pressMoved = true;
        updateNavigationButtons();
    }
    if (!m_dragging)
        return;
    m_pan = m_panOrigin + (e->position() - m_dragOrigin) * devicePixelRatio();
    clampPan();
    requestUpdate();
}

void ViewerWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    const Zone pressed = std::exchange(m_pressZone, Zone::None);
    const Zone released = zoneAt(e->position());
    if (pressed != Zone::None && !m_pressMoved && released == pressed)
        step(pressed == Zone::Next ? +1 : -1);
    // Refresh unconditionally: the button loses its pressed look even when the zone is unchanged.
    m_hoverZone = released;
    updateCursor();
    updateNavigationButtons();
}

void ViewerWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (zoneAt(e->position()) != Zone::None) {
        mousePressEvent(e); // quick clicks on a side keep navigating
        return;
    }
    // The first click may have reached the end of a folder that does not loop: the second
    // click of the pair is still a click on the side, never a request for full screen.
    if (inSideStrip(e->position()))
        return;
    toggleFullScreen();
}

void ViewerWindow::wheelEvent(QWheelEvent *e)
{
    // Scroll gestures (trackpads: they have phases or a touchpad device) pan; wheels and
    // Ctrl+scroll zoom. pixelDelta alone says nothing: macOS sets it for mouse wheels too.
    pointerActive();
    const QPointingDevice *device = e->pointingDevice();
    const bool gesture = e->phase() != Qt::NoScrollPhase
                         || (device && device->type() == QInputDevice::DeviceType::TouchPad);
    if (gesture && !(e->modifiers() & Qt::ControlModifier)) {
        if (canPan()) {
            const QPointF delta = !e->pixelDelta().isNull() ? QPointF(e->pixelDelta())
                                                            : QPointF(e->angleDelta()) / 8.0; // degrees ~ pixels
            m_pan += delta * devicePixelRatio();
            clampPan();
            requestUpdate();
        }
        return;
    }
    const double steps = e->angleDelta().y() / 120.0;
    if (steps != 0.0)
        zoomAt(std::pow(1.25, steps), e->position() * devicePixelRatio());
}

void ViewerWindow::toggleFullScreen()
{
    if (visibility() == QWindow::FullScreen) {
        leaveFullScreen();
        return;
    }
    m_maximizedBeforeFullScreen = windowStates().testFlag(Qt::WindowMaximized);
    showFullScreen();
}

void ViewerWindow::leaveFullScreen()
{
    if (m_maximizedBeforeFullScreen)
        showMaximized();
    else
        showNormal();
}
