#include "viewer.h"

#include "folder.h"

#include <QCursor>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPlatformSurfaceEvent>
#include <QScreen>
#include <QStyleHints>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int kDefaultMaxTexture = 16384;
constexpr double kMinZoom = 0.01;
constexpr double kMaxZoom = 64.0;
constexpr float kMaxExposureEv = 16.0f;
constexpr double kNavigationButtonSize = 56.0; // logical pixels
constexpr int kNoticeMs = 3000;

// "100 %" only when the image really is shown 1:1 (nearest sampling); otherwise one decimal.
QString zoomLabel(double zoom)
{
    return zoom == 1.0 ? QLocale().toString(100) : QLocale().toString(zoom * 100.0, 'f', 1);
}

// The round previous/next button of a side zone, drawn at `pixels` (device) size.
QImage navigationButton(bool next, int pixels, bool pressed)
{
    QImage image(pixels, pixels, QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(pixels / kNavigationButtonSize);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, pressed ? 180 : 125));
    const double s = kNavigationButtonSize;
    painter.drawEllipse(QRectF(0.5, 0.5, s - 1.0, s - 1.0));
    painter.setPen(QPen(QColor(240, 240, 240), 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const double c = s / 2.0, half = s * 0.16, depth = s * 0.08;
    const double dir = next ? 1.0 : -1.0;
    QPainterPath chevron;
    chevron.moveTo(c - dir * depth, c - half);
    chevron.lineTo(c + dir * depth, c);
    chevron.lineTo(c - dir * depth, c + half);
    painter.drawPath(chevron);
    return image;
}
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
    setTitle(QStringLiteral("imageViewer"));
    setMinimumSize(QSize(320, 240));
    m_toneMap = m_settings.toneMap;
    m_showInfo = m_settings.showInfo;
    m_renderer.setOutputPreference(m_settings.output);
    m_decodePool.setMaxThreadCount(1);
    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(kNoticeMs);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] {
        m_notice.clear();
        updateOverlay();
    });
    connect(&m_watcher, &QFutureWatcher<Image>::finished, this, &ViewerWindow::imageDecoded);
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
    m_watcher.waitForFinished();
    m_copyWatcher.waitForFinished();
}

void ViewerWindow::showRestored(const SessionState &session)
{
    m_lastDirectory = session.lastDirectory;
    bool placed = false;
    if (m_settings.rememberGeometry && session.geometry.isValid()) {
        // Only where a screen still shows a usable part of it: monitors change between sessions.
        const QList<QScreen *> screens = QGuiApplication::screens();
        placed = std::any_of(screens.cbegin(), screens.cend(), [&session](const QScreen *screen) {
            const QRect visible = screen->availableGeometry().intersected(session.geometry);
            return visible.width() >= 160 && visible.height() >= 120;
        });
    }
    if (placed)
        setGeometry(session.geometry);
    else
        resize(1280, 800);
    m_normalGeometry = geometry();
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
    session.maximized = windowStates().testFlag(Qt::WindowMaximized);
    session.fullScreen = windowStates().testFlag(Qt::WindowFullScreen);
    session.lastFile = m_image.path;
    session.lastDirectory = m_lastDirectory;
    session.save();
    m_settings.save(); // includes the information panel toggled with I
}

void ViewerWindow::openFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        m_message = tr("File not found: %1").arg(path);
        updateOverlay();
        return;
    }
    if (info.isDir()) {
        QStringList files = listImages(info.absoluteFilePath());
        if (files.isEmpty()) { // keep the current list and image
            m_message = tr("The folder contains no supported images.");
            updateOverlay();
            return;
        }
        m_files = std::move(files);
        m_lastDirectory = info.absoluteFilePath();
        startLoading(0);
        return;
    }
    m_files = listImages(info.absolutePath());
    m_lastDirectory = info.absolutePath();
    // Same directory, so the name decides; Windows and macOS file systems ignore case.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    constexpr Qt::CaseSensitivity kCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity kCase = Qt::CaseSensitive;
#endif
    const QString name = info.fileName();
    const auto it = std::find_if(m_files.cbegin(), m_files.cend(), [&name](const QString &f) {
        return QStringView(f).mid(f.lastIndexOf(QLatin1Char('/')) + 1).compare(name, kCase) == 0;
    });
    if (it == m_files.cend()) {
        m_files.prepend(info.absoluteFilePath()); // unknown suffix: still try to decode it
        startLoading(0);
    } else {
        startLoading(int(it - m_files.cbegin()));
    }
}

int ViewerWindow::textureLimit(const QString &path) const
{
    int limit = m_rendererReady ? std::min(kDefaultMaxTexture, m_renderer.maxTextureSize()) : kDefaultMaxTexture;
    if (m_textureCap > 0 && path == m_textureCapPath)
        limit = std::min(limit, m_textureCap);
    return limit;
}

void ViewerWindow::startLoading(int index)
{
    if (index < 0 || index >= m_files.size())
        return;
    m_index = index;
    m_message = tr("Loading %1…").arg(QFileInfo(m_files.at(index)).fileName());
    updateOverlay();
    // One decode at a time: decodes cannot be cancelled, and key auto-repeat would otherwise
    // stack several full-resolution decodes in memory. The latest request waits its turn.
    if (m_watcher.isRunning()) {
        m_pendingIndex = index;
        return;
    }
    m_pendingIndex = -1;
    const QString path = m_files.at(index);
    const int maxTexture = textureLimit(path);
    m_watcher.setFuture(QtConcurrent::run(&m_decodePool, [path, maxTexture] { return decodeImage(path, maxTexture); }));
}

void ViewerWindow::imageDecoded()
{
    // takeResult() moves the pixels out; result() would copy them and the future would
    // keep its own copy alive until the next decode.
    Image image = m_watcher.future().takeResult();
    if (m_pendingIndex >= 0) { // superseded while decoding: go straight to the latest request
        const int next = std::exchange(m_pendingIndex, -1);
        if (m_files.value(next) != image.path) {
            startLoading(next);
            return;
        }
        m_index = next; // the request came back to the file just decoded
    }
    setTitle(QStringLiteral("%1 — imageViewer").arg(QFileInfo(image.path).fileName()));

    // The same file again (smaller texture, device loss, language) keeps the view; a new file starts fitted.
    const bool sameFile = image.path == m_image.path;
    if (!image.isValid()) {
        m_message = image.error;
        m_image = Image();
        m_image.path = image.path;
        m_renderer.clearImage();
    } else {
        m_message.clear();
        m_renderer.setImage(std::move(image.pixels), QSize(image.width, image.height));
        m_image = std::move(image);
        if (!sameFile) {
            m_fit = true;
            m_pan = {};
            m_quarterTurns = 0;
            m_mirrored = false;
        }
    }
    // At either end of a non-looping folder a side button may have nothing left to do.
    setHoverZone(zoneAt(mapFromGlobal(QCursor::pos(screen()))));
    updateOverlay();
    requestUpdate();
}

bool ViewerWindow::hasNeighbour(int delta) const
{
    if (m_files.size() < 2)
        return false;
    const int target = m_index + delta;
    return m_settings.loop || (target >= 0 && target < m_files.size());
}

void ViewerWindow::step(int delta)
{
    if (m_files.isEmpty())
        return;
    const int n = int(m_files.size());
    int target = m_index + delta;
    if (target < 0 || target >= n) {
        if (!m_settings.loop) {
            showNotice(delta > 0 ? tr("This is the last image.") : tr("This is the first image."));
            return;
        }
        target = (target % n + n) % n;
    }
    startLoading(target);
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
    case QEvent::Leave:
        setHoverZone(Zone::None);
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
            QMessageBox::critical(nullptr, QStringLiteral("imageViewer"), error);
            QCoreApplication::exit(1);
        }, Qt::QueuedConnection);
        return;
    }
    m_rendererReady = true;
    updateOverlay();
}

void ViewerWindow::resizeEvent(QResizeEvent *)
{
    if (windowStates() == Qt::WindowNoState)
        m_normalGeometry = geometry();
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
    return QSizeF(size()) * devicePixelRatio();
}

QSizeF ViewerWindow::displayedImageSize() const
{
    const QSizeF s(m_image.width, m_image.height);
    return (m_quarterTurns % 2) ? s.transposed() : s;
}

double ViewerWindow::fitZoom() const
{
    const QSizeF image = displayedImageSize(), view = deviceSize();
    if (image.isEmpty() || view.isEmpty())
        return 1.0;
    return std::min({view.width() / image.width(), view.height() / image.height(), 1.0});
}

double ViewerWindow::currentZoom() const
{
    return m_fit ? fitZoom() : m_zoom;
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
    m_fit = false;
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
    m_fit = false;
    clampPan();
    updateOverlay();
}

void ViewerWindow::setFit()
{
    m_fit = true;
    m_pan = {};
    updateOverlay();
    requestUpdate();
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
    frame.toneMap = m_toneMap;
    frame.clipWarning = m_clipWarning;
    frame.contentPeak = m_image.maxComponent;
    frame.contentLuminancePeak = m_image.maxLuminance;
    frame.absoluteLuminance = m_image.colour.isAbsolute();
    frame.background[0] = float(m_settings.background.redF());
    frame.background[1] = float(m_settings.background.greenF());
    frame.background[2] = float(m_settings.background.blueF());
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
    }
    if (!m_overlaySize.isEmpty()) {
        const double margin = 12.0 * devicePixelRatio();
        const double x = QGuiApplication::layoutDirection() == Qt::RightToLeft
                             ? deviceSize().width() - margin - m_overlaySize.width()
                             : margin;
        frame.overlayRects[Renderer::InfoLayer] =
            QRectF(QPointF(std::round(x), std::round(deviceSize().height() - margin - m_overlaySize.height())),
                   QSizeF(m_overlaySize));
    }
    if (m_hoverZone == Zone::Previous)
        frame.overlayRects[Renderer::PreviousButtonLayer] = zoneButtonRect(Zone::Previous);
    else if (m_hoverZone == Zone::Next)
        frame.overlayRects[Renderer::NextButtonLayer] = zoneButtonRect(Zone::Next);
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
        const int index = int(m_files.indexOf(m_image.path));
        if (m_textureCap >= 512 && index >= 0) {
            startLoading(index);
            m_message = tr("Reducing the image to fit the GPU (at most %1 px)…").arg(m_textureCap);
        } else {
            m_message = tr("The GPU did not accept the image.");
            m_image = Image();
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
    qWarning("imageViewer: graphics device lost; reinitialising the renderer");
    m_renderer.releaseResources();
    m_rendererReady = false;
    initializeRenderer();
    if (m_rendererReady) {
        updateNavigationButtons();
        if (m_index >= 0)
            startLoading(m_index);
    }
    requestUpdate();
}

void ViewerWindow::showNotice(const QString &text)
{
    m_notice = text;
    m_noticeTimer.start();
    updateOverlay();
}

void ViewerWindow::updateOverlay()
{
    if (!m_rendererReady)
        return;
    m_overlayOutput = m_renderer.output().description;
    const QLocale locale;
    QStringList lines;
    if (!m_notice.isEmpty())
        lines << m_notice;
    if (!m_message.isEmpty())
        lines << m_message;
    if (m_showInfo && m_image.width > 0) {
        QString first = QStringLiteral("%1  ·  %2/%3  ·  %4 %  ·  %5×%6")
                            .arg(QFileInfo(m_image.path).fileName(), locale.toString(m_index + 1),
                                 locale.toString(m_files.size()), zoomLabel(currentZoom()),
                                 locale.toString(m_image.sourceWidth), locale.toString(m_image.sourceHeight));
        if (m_image.width != m_image.sourceWidth)
            first += QLatin1Char(' ')
                     + tr("(reduced to %1×%2)").arg(locale.toString(m_image.width), locale.toString(m_image.height));
        lines << first;
        //: Bits per channel of the image file, e.g. "16-bit".
        QStringList format = {tr("%1-bit").arg(m_image.sourceBits)};
        if (m_image.sourceFloat)
            format << tr("floating point");
        if (m_image.hasAlpha)
            format << tr("alpha");
        lines << QStringLiteral("%1  ·  %2  ·  %3").arg(m_image.codec, format.join(QStringLiteral(", ")),
                                                      m_image.colour.description);
        lines << tr("Peak %1× SDR white (≈%2 nits)").arg(locale.toString(double(m_image.maxComponent), 'f', 2),
                                                      locale.toString(double(m_image.maxComponent
                                                                             * color::kSdrReferenceWhiteNits),
                                                                      'f', 0))
                     + QStringLiteral("  ·  ")
                     + tr("decoded in %1 ms").arg(locale.toString(m_image.decodeMs, 'f', 0));
    }
    if (m_showInfo) {
        //: %1: the display output, e.g. "HDR10 (PQ) · SDR white 203 nits · peak 1000 nits".
        QStringList output = {tr("Output: %1").arg(m_renderer.output().description), m_renderer.backendName()};
        if (m_exposureEv != 0.0f)
            output << tr("exposure %1 EV")
                          .arg((m_exposureEv > 0 ? QStringLiteral("+") : QString())
                               + locale.toString(double(m_exposureEv), 'f', 1));
        if (m_clipWarning)
            output << tr("altered pixels highlighted");
        lines << output.join(QStringLiteral("  ·  "));
        if (m_image.width > 0) {
            // Whether the image is shown as is, tone mapped or clipped (criteria H4, H6).
            const Renderer::Output &out = m_renderer.output();
            const color::OutputStage stage = Renderer::stageFor(out, imageFrame());
            const double toNits = out.nitsPerUnit;
            const double peakNits = stage.peak * toNits;
            if (stage.sourcePeak > stage.peak) {
                QString line = tr("BT.2390 tone mapping: %1 → %2 nits, unchanged up to %3 nits")
                                   .arg(locale.toString(stage.sourcePeak * toNits, 'f', 0),
                                        locale.toString(peakNits, 'f', 0),
                                        locale.toString(color::eetfKneeNits(float(stage.sourcePeak * toNits),
                                                                            float(peakNits)),
                                                        'f', 0));
                if (m_image.maxComponent * stage.exposure * stage.scale * toNits > color::kPqPeakNits)
                    line += QLatin1Char(' ')
                            + tr("(clipped above %1 nits)").arg(locale.toString(double(color::kPqPeakNits), 'f', 0));
                lines << line;
            } else if (m_image.maxComponent * stage.exposure * stage.scale > stage.peak) {
                lines << (m_toneMap ? tr("Components above %1 nits clipped (color outside the output gamut)")
                                    : tr("Tone mapping off: values above %1 nits clipped"))
                             .arg(locale.toString(peakNits, 'f', 0));
            }
        }
    }

    if (lines.isEmpty()) {
        m_overlaySize = {};
        m_renderer.setOverlay(Renderer::InfoLayer, QImage());
        requestUpdate();
        return;
    }

    const qreal dpr = devicePixelRatio();
    const QFont font = QGuiApplication::font();
    const QFontMetricsF metrics(font);
    const qreal padding = 10.0, lineHeight = metrics.height() + 2.0;
    qreal width = 0;
    for (const QString &line : std::as_const(lines))
        width = std::max(width, metrics.horizontalAdvance(line));
    // Never wider than the window (long file names): the panel clips instead.
    const qreal maxWidth = std::max(80.0, this->width() - 24.0);
    const QSizeF logical(std::ceil(std::min(width + 2 * padding, maxWidth)),
                         std::ceil(lines.size() * lineHeight + 2 * padding - 2.0));

    QImage overlay((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    overlay.setDevicePixelRatio(dpr);
    overlay.fill(Qt::transparent);
    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setLayoutDirection(QGuiApplication::layoutDirection());
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawRoundedRect(QRectF(QPointF(0, 0), logical), 8, 8);
    painter.setPen(QColor(235, 235, 235));
    painter.setFont(font);
    const Qt::Alignment align =
        (QGuiApplication::layoutDirection() == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop;
    for (int i = 0; i < lines.size(); ++i)
        painter.drawText(QRectF(padding, padding + i * lineHeight, logical.width() - 2 * padding, lineHeight), align,
                         lines.at(i));
    painter.end();

    m_overlaySize = overlay.size();
    m_renderer.setOverlay(Renderer::InfoLayer, overlay);
    requestUpdate();
}

ViewerWindow::Zone ViewerWindow::zoneAt(const QPointF &position) const
{
    if (!m_settings.sideZones || m_files.size() < 2 || position.y() < 0 || position.y() >= height())
        return Zone::None;
    // Narrow windows keep a middle for panning and double-clicks.
    const double zone = std::min<double>(m_settings.sideZoneWidth, width() / 4.0);
    if (position.x() >= 0 && position.x() < zone)
        return hasNeighbour(-1) ? Zone::Previous : Zone::None;
    if (position.x() < width() && position.x() >= width() - zone)
        return hasNeighbour(+1) ? Zone::Next : Zone::None;
    return Zone::None;
}

QRectF ViewerWindow::zoneButtonRect(Zone zone) const
{
    const double dpr = devicePixelRatio();
    const double pixels = std::round(kNavigationButtonSize * dpr);
    const double zoneWidth = std::min<double>(m_settings.sideZoneWidth, width() / 4.0) * dpr;
    const double centreX = zone == Zone::Previous ? zoneWidth / 2.0 : deviceSize().width() - zoneWidth / 2.0;
    // Whole device pixels: the button texture is sampled 1:1.
    return QRectF(QPointF(std::round(centreX - pixels / 2.0), std::round((deviceSize().height() - pixels) / 2.0)),
                  QSizeF(pixels, pixels));
}

void ViewerWindow::setHoverZone(Zone zone)
{
    if (zone == m_hoverZone)
        return;
    m_hoverZone = zone;
    setCursor(zone == Zone::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
    updateNavigationButtons();
}

void ViewerWindow::updateNavigationButtons()
{
    if (!m_rendererReady)
        return;
    const int pixels = int(std::lround(kNavigationButtonSize * devicePixelRatio()));
    const bool pressed = m_pressZone != Zone::None && m_pressZone == m_hoverZone && !m_pressMoved;
    m_renderer.setOverlay(Renderer::PreviousButtonLayer, m_hoverZone == Zone::Previous
                                                             ? navigationButton(false, pixels, pressed)
                                                             : QImage());
    m_renderer.setOverlay(Renderer::NextButtonLayer,
                          m_hoverZone == Zone::Next ? navigationButton(true, pixels, pressed) : QImage());
    requestUpdate();
}

void ViewerWindow::applySettings(const Settings &settings)
{
    const Settings previous = m_settings;
    m_settings = settings;
    m_settings.save();
    if (settings.language != previous.language) {
        applyLanguage(settings.language);
        // Descriptions are composed while decoding: decode again for the new language.
        if (currentFileIsShown() && m_image.width > 0)
            startLoading(m_index);
    }
    if (settings.toneMap != previous.toneMap)
        m_toneMap = settings.toneMap;
    m_showInfo = settings.showInfo;
    m_renderer.setOutputPreference(settings.output);
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
    requestUpdate();
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
    m_toneMap = !m_toneMap;
    updateOverlay();
}

void ViewerWindow::toggleClipWarning()
{
    m_clipWarning = !m_clipWarning;
    updateOverlay();
}

void ViewerWindow::toggleInfo()
{
    m_showInfo = !m_showInfo;
    m_settings.showInfo = m_showInfo;
    updateOverlay();
}

void ViewerWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && e->modifiers() == Qt::NoModifier && visibility() == QWindow::FullScreen) {
        showNormal();
        return;
    }
    if (!executeShortcut(e))
        QWindow::keyPressEvent(e);
}

void ViewerWindow::mousePressEvent(QMouseEvent *e)
{
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
    m_dragging = !m_fit;
    if (m_pressZone != Zone::None)
        updateNavigationButtons(); // pressed look
}

void ViewerWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (!(e->buttons() & Qt::LeftButton)) {
        // No press in progress (or its release went elsewhere, e.g. to the context menu).
        m_dragging = false;
        m_pressZone = Zone::None;
        setHoverZone(zoneAt(e->position()));
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
    setCursor(released == Zone::None ? Qt::ArrowCursor : Qt::PointingHandCursor);
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
    toggleFullScreen();
}

void ViewerWindow::wheelEvent(QWheelEvent *e)
{
    // Scroll gestures (trackpads: they have phases or a touchpad device) pan; wheels and
    // Ctrl+scroll zoom. pixelDelta alone says nothing: macOS sets it for mouse wheels too.
    const QPointingDevice *device = e->pointingDevice();
    const bool gesture = e->phase() != Qt::NoScrollPhase
                         || (device && device->type() == QInputDevice::DeviceType::TouchPad);
    if (gesture && !(e->modifiers() & Qt::ControlModifier)) {
        if (!m_fit) {
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
    if (visibility() == QWindow::FullScreen)
        showNormal();
    else
        showFullScreen();
}
