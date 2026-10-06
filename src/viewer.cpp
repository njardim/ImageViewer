#include "viewer.h"

#include "folder.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kDefaultMaxTexture = 16384;
constexpr double kMinZoom = 0.01;
constexpr double kMaxZoom = 64.0;
constexpr double kZoomStep = 1.25;
} // namespace

ViewerWindow::ViewerWindow(QVulkanInstance *vulkan) : m_renderer(this)
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
    connect(&m_watcher, &QFutureWatcher<Image>::finished, this, &ViewerWindow::imageDecoded);
    connect(this, &QWindow::screenChanged, this, [this] {
        if (m_rendererReady)
            m_renderer.refreshOutput();
        updateOverlay();
    });
}

ViewerWindow::~ViewerWindow()
{
    m_watcher.waitForFinished();
}

void ViewerWindow::openFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        m_message = tr("Ficheiro não encontrado: %1").arg(path);
        updateOverlay();
        return;
    }
    if (info.isDir()) {
        m_files = listImages(info.absoluteFilePath());
        if (m_files.isEmpty()) {
            m_message = tr("A pasta não contém imagens suportadas.");
            updateOverlay();
            return;
        }
        startLoading(0);
        return;
    }
    m_files = listImages(info.absolutePath());
    // QFileInfo comparison follows the file system's case sensitivity.
    const auto it = std::find_if(m_files.cbegin(), m_files.cend(),
                                 [&info](const QString &f) { return QFileInfo(f) == info; });
    if (it == m_files.cend()) {
        m_files.prepend(info.absoluteFilePath()); // unknown suffix: still try to decode it
        startLoading(0);
    } else {
        startLoading(int(it - m_files.cbegin()));
    }
}

void ViewerWindow::startLoading(int index)
{
    if (index < 0 || index >= m_files.size())
        return;
    m_index = index;
    m_loadingPath = m_files.at(index);
    const int maxTexture = m_rendererReady ? std::min(kDefaultMaxTexture, m_renderer.maxTextureSize())
                                           : kDefaultMaxTexture;
    const QString path = m_loadingPath;
    m_watcher.setFuture(QtConcurrent::run([path, maxTexture] { return decodeImage(path, maxTexture); }));
    m_message = tr("A carregar %1…").arg(QFileInfo(path).fileName());
    updateOverlay();
}

void ViewerWindow::imageDecoded()
{
    Image image = m_watcher.result();
    if (image.path != m_loadingPath)
        return; // superseded by a newer request
    m_loadingPath.clear();
    setTitle(QStringLiteral("%1 — imageViewer").arg(QFileInfo(image.path).fileName()));

    if (!image.isValid()) {
        m_message = image.error;
        m_image = Image();
        m_image.path = image.path;
        m_renderer.clearImage();
    } else {
        m_message.clear();
        m_renderer.setImage(std::move(image.pixels), QSize(image.width, image.height));
        image.pixels = {};
        m_image = std::move(image);
        m_fit = true;
        m_pan = {};
        m_quarterTurns = 0;
        m_mirrored = false;
    }
    updateOverlay();
    requestUpdate();
}

void ViewerWindow::step(int delta)
{
    if (m_files.isEmpty())
        return;
    const int n = int(m_files.size());
    startLoading(((m_index + delta) % n + n) % n);
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
        if (drop->mimeData()->hasUrls()) {
            drop->acceptProposedAction();
            return true;
        }
        break;
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
        QMessageBox::critical(nullptr, QStringLiteral("imageViewer"), error);
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
        return;
    }
    m_rendererReady = true;
    updateOverlay();
}

void ViewerWindow::resizeEvent(QResizeEvent *)
{
    clampPan();
    updateOverlay(); // the fit zoom shown in the overlay may have changed
    requestUpdate();
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
    zoomAt(1.0 / currentZoom(), QPointF(deviceSize().width(), deviceSize().height()) / 2.0);
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
        frame.overlayRect = QRectF(QPointF(margin, deviceSize().height() - margin - m_overlaySize.height()),
                                   QSizeF(m_overlaySize));
    }
    m_renderer.render(frame);
    // The output is only known once the swapchain exists, and changes with the screen.
    if (m_renderer.output().description != m_overlayOutput)
        updateOverlay();
}

void ViewerWindow::updateOverlay()
{
    if (!m_rendererReady)
        return;
    m_overlayOutput = m_renderer.output().description;
    QStringList lines;
    if (!m_message.isEmpty())
        lines << m_message;
    if (m_showInfo && m_image.width > 0) {
        QString first = QStringLiteral("%1  ·  %2/%3  ·  %4 %  ·  %5×%6")
                            .arg(QFileInfo(m_image.path).fileName())
                            .arg(m_index + 1)
                            .arg(m_files.size())
                            .arg(qRound(currentZoom() * 100.0))
                            .arg(m_image.sourceWidth)
                            .arg(m_image.sourceHeight);
        if (m_image.width != m_image.sourceWidth)
            first += tr("  (reduzida a %1×%2)").arg(m_image.width).arg(m_image.height);
        lines << first;
        lines << QStringLiteral("%1  ·  %2 bits%3%4  ·  %5")
                     .arg(m_image.codec)
                     .arg(m_image.sourceBits)
                     .arg(m_image.sourceFloat ? QStringLiteral(" float") : QString())
                     .arg(m_image.hasAlpha ? tr(" + alfa") : QString())
                     .arg(m_image.colour.description);
        lines << tr("Máximo %1× o branco SDR (≈%2 nits)  ·  descodificação %3 ms")
                     .arg(double(m_image.maxComponent), 0, 'f', 2)
                     .arg(double(m_image.maxComponent * color::kSdrReferenceWhiteNits), 0, 'f', 0)
                     .arg(m_image.decodeMs, 0, 'f', 0);
    }
    if (m_showInfo) {
        QString output = tr("Saída: %1  ·  %2").arg(m_renderer.output().description, m_renderer.backendName());
        if (m_exposureEv != 0.0f)
            output += tr("  ·  exposição %1%2 EV").arg(m_exposureEv > 0 ? "+" : "").arg(double(m_exposureEv), 0, 'f', 1);
        if (m_clipWarning)
            output += tr("  ·  aviso de píxeis alterados");
        lines << output;
        if (m_image.width > 0) {
            // Whether the image is shown as is, tone mapped or clipped (criteria H4, H6).
            const Renderer::Output &out = m_renderer.output();
            const color::OutputStage stage = Renderer::stageFor(out, imageFrame());
            const double toNits = out.nitsPerUnit;
            const double peakNits = stage.peak * toNits;
            if (stage.sourcePeak > stage.peak) {
                lines << tr("Tone mapping BT.2390: %1 → %2 nits, idêntico até %3 nits")
                             .arg(stage.sourcePeak * toNits, 0, 'f', 0)
                             .arg(peakNits, 0, 'f', 0)
                             .arg(color::eetfKneeNits(float(stage.sourcePeak * toNits), float(peakNits)), 0, 'f', 0);
            } else if (m_image.maxComponent * stage.exposure * stage.scale > stage.peak) {
                lines << (m_toneMap ? tr("Componentes acima de %1 nits cortados (cor fora da gama da saída)")
                                    : tr("Tone mapping desligado: valores acima de %1 nits cortados"))
                             .arg(peakNits, 0, 'f', 0);
            }
        }
    }

    if (lines.isEmpty()) {
        m_overlaySize = {};
        m_renderer.setOverlay(QImage());
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
    const QSizeF logical(std::ceil(width + 2 * padding), std::ceil(lines.size() * lineHeight + 2 * padding - 2.0));

    QImage overlay((logical * dpr).toSize(), QImage::Format_RGBA8888_Premultiplied);
    overlay.setDevicePixelRatio(dpr);
    overlay.fill(Qt::transparent);
    QPainter painter(&overlay);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 150));
    painter.drawRoundedRect(QRectF(QPointF(0, 0), logical), 8, 8);
    painter.setPen(QColor(235, 235, 235));
    painter.setFont(font);
    for (int i = 0; i < lines.size(); ++i)
        painter.drawText(QPointF(padding, padding + metrics.ascent() + i * lineHeight), lines.at(i));
    painter.end();

    m_overlaySize = overlay.size();
    m_renderer.setOverlay(overlay);
    requestUpdate();
}

void ViewerWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::Open)) {
        showOpenDialog();
        return;
    }
    if (e->matches(QKeySequence::Quit) || (e->key() == Qt::Key_Q && e->modifiers() == Qt::ControlModifier)) {
        close();
        return;
    }
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const QPointF centre = QPointF(deviceSize().width(), deviceSize().height()) / 2.0;
    switch (e->key()) {
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space: step(+1); break;
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace: step(-1); break;
    case Qt::Key_Home: startLoading(0); break;
    case Qt::Key_End: startLoading(int(m_files.size()) - 1); break;
    case Qt::Key_0: setFit(); break;
    case Qt::Key_1: setActualSize(); break;
    case Qt::Key_Plus:
    case Qt::Key_Equal: zoomAt(kZoomStep, centre); break;
    case Qt::Key_Minus: zoomAt(1.0 / kZoomStep, centre); break;
    case Qt::Key_F:
    case Qt::Key_F11: toggleFullScreen(); break;
    case Qt::Key_Escape:
        if (visibility() == QWindow::FullScreen)
            showNormal();
        break;
    case Qt::Key_R:
        m_quarterTurns = (m_quarterTurns + (shift ? 3 : 1)) % 4;
        clampPan();
        updateOverlay();
        requestUpdate();
        break;
    case Qt::Key_H:
        m_mirrored = !m_mirrored;
        requestUpdate();
        break;
    case Qt::Key_I:
        m_showInfo = !m_showInfo;
        updateOverlay();
        break;
    case Qt::Key_E:
        m_exposureEv += shift ? -0.5f : 0.5f;
        updateOverlay();
        break;
    case Qt::Key_C:
        m_clipWarning = !m_clipWarning;
        updateOverlay();
        break;
    case Qt::Key_T:
        m_toneMap = !m_toneMap;
        updateOverlay();
        break;
    default:
        QWindow::keyPressEvent(e);
        return;
    }
}

void ViewerWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        showContextMenu(e->globalPosition().toPoint());
        return;
    }
    if (e->button() == Qt::LeftButton && !m_fit) {
        m_dragging = true;
        m_dragOrigin = e->position();
        m_panOrigin = m_pan;
    }
}

void ViewerWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    m_pan = m_panOrigin + (e->position() - m_dragOrigin) * devicePixelRatio();
    clampPan();
    requestUpdate();
}

void ViewerWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        m_dragging = false;
}

void ViewerWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        toggleFullScreen();
}

void ViewerWindow::wheelEvent(QWheelEvent *e)
{
    const bool trackpadScroll = !e->pixelDelta().isNull() && !(e->modifiers() & Qt::ControlModifier);
    if (trackpadScroll) {
        if (!m_fit) {
            m_pan += QPointF(e->pixelDelta()) * devicePixelRatio();
            clampPan();
            requestUpdate();
        }
        return;
    }
    const double steps = e->angleDelta().y() / 120.0;
    if (steps != 0.0)
        zoomAt(std::pow(kZoomStep, steps), e->position() * devicePixelRatio());
}

void ViewerWindow::toggleFullScreen()
{
    if (visibility() == QWindow::FullScreen)
        showNormal();
    else
        showFullScreen();
}

void ViewerWindow::showOpenDialog()
{
    QStringList patterns;
    for (const QString &suffix : supportedSuffixes())
        patterns << QStringLiteral("*.") + suffix;
    const QString filter = tr("Imagens (%1);;Todos os ficheiros (*)").arg(patterns.join(QLatin1Char(' ')));
    const QString start = m_image.path.isEmpty()
                              ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                              : QFileInfo(m_image.path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("Abrir imagem"), start, filter);
    if (!path.isEmpty())
        openFile(path);
}

void ViewerWindow::showContextMenu(const QPoint &globalPos)
{
    QMenu menu;
    auto add = [&](const QString &text, const QKeySequence &shortcut, auto slot, bool checked = false,
                   bool checkable = false) {
        QAction *action = menu.addAction(text);
        action->setShortcut(shortcut);
        action->setCheckable(checkable);
        action->setChecked(checked);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    const QPointF centre = QPointF(deviceSize().width(), deviceSize().height()) / 2.0;
    add(tr("Abrir…"), QKeySequence::Open, [this] { showOpenDialog(); });
    menu.addSeparator();
    add(tr("Ajustar à janela"), QKeySequence(Qt::Key_0), [this] { setFit(); });
    add(tr("Tamanho real (100 %)"), QKeySequence(Qt::Key_1), [this] { setActualSize(); });
    add(tr("Aproximar"), QKeySequence(Qt::Key_Plus), [this, centre] { zoomAt(kZoomStep, centre); });
    add(tr("Afastar"), QKeySequence(Qt::Key_Minus), [this, centre] { zoomAt(1.0 / kZoomStep, centre); });
    menu.addSeparator();
    add(tr("Rodar para a direita"), QKeySequence(Qt::Key_R), [this] {
        m_quarterTurns = (m_quarterTurns + 1) % 4;
        clampPan();
        updateOverlay();
    });
    add(tr("Rodar para a esquerda"), QKeySequence(Qt::SHIFT | Qt::Key_R), [this] {
        m_quarterTurns = (m_quarterTurns + 3) % 4;
        clampPan();
        updateOverlay();
    });
    add(tr("Espelhar"), QKeySequence(Qt::Key_H), [this] {
        m_mirrored = !m_mirrored;
        requestUpdate();
    }, m_mirrored, true);
    menu.addSeparator();
    add(tr("Exposição +½ EV"), QKeySequence(Qt::Key_E), [this] {
        m_exposureEv += 0.5f;
        updateOverlay();
    });
    add(tr("Exposição −½ EV"), QKeySequence(Qt::SHIFT | Qt::Key_E), [this] {
        m_exposureEv -= 0.5f;
        updateOverlay();
    });
    add(tr("Repor exposição"), QKeySequence(), [this] {
        m_exposureEv = 0.0f;
        updateOverlay();
    });
    add(tr("Tone mapping (BT.2390)"), QKeySequence(Qt::Key_T), [this] {
        m_toneMap = !m_toneMap;
        updateOverlay();
    }, m_toneMap, true);
    add(tr("Aviso de píxeis alterados (clip ou tone mapping)"), QKeySequence(Qt::Key_C), [this] {
        m_clipWarning = !m_clipWarning;
        updateOverlay();
    }, m_clipWarning, true);
    add(tr("Informação"), QKeySequence(Qt::Key_I), [this] {
        m_showInfo = !m_showInfo;
        updateOverlay();
    }, m_showInfo, true);
    add(tr("Ecrã inteiro"), QKeySequence(Qt::Key_F), [this] { toggleFullScreen(); },
        visibility() == QWindow::FullScreen, true);
    menu.addSeparator();
    add(tr("Sair"), QKeySequence::Quit, [this] { close(); });

    menu.winId(); // create the native window so it can be parented (needed on Wayland)
    if (QWindow *handle = menu.windowHandle())
        handle->setTransientParent(this);
    menu.exec(globalPos);
}
