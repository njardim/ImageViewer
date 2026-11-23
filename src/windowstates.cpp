// ViewerWindow, part 10: the window's states (D-08: gathered from viewer.cpp, D-114): shown where
// the session left it, and the session saved; the window fitted to the image (D-51); full screen
// and back, the image gliding from the place it had on the screen to its new one rather than
// jumping (the owner, D-114), Windows' own animation of the window held back meanwhile; minimized
// and back.
#include "viewer.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

#if defined(Q_OS_WIN)
#include <qt_windows.h> // windows.h, with NOMINMAX
#include <dwmapi.h>
#endif

namespace {

constexpr int kGlideMs = 200;      // the image's glide into its new place
constexpr int kGlideWaitMs = 500;  // ...after the window changes, if it does within this

} // namespace

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
    // Closed while minimized (from the taskbar or the Dock), it opens again as it was before.
    Qt::WindowStates states = windowStates();
    if (states.testFlag(Qt::WindowMinimized))
        states |= m_statesBeforeMinimize;
    const bool fullScreen = states.testFlag(Qt::WindowFullScreen);
    session.fullScreen = fullScreen && !m_slideshowLeavesFullScreen; // the slideshow's own ends with it
    session.maximized = fullScreen ? m_maximizedBeforeFullScreen : states.testFlag(Qt::WindowMaximized);
    session.lastFile = m_image.path;
    session.lastDirectory = m_lastDirectory;
    session.save(); // preferences are saved when they change, never here (U2)
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
    QSizeF size = displayedImageSize() * naturalZoom() / devicePixelRatio();
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

void ViewerWindow::toggleFullScreen()
{
    if (visibility() == QWindow::FullScreen) {
        leaveFullScreen();
        return;
    }
    m_maximizedBeforeFullScreen = windowStates().testFlag(Qt::WindowMaximized);
    startGlide();
    showFullScreen();
    presentChangedWindow();
}

void ViewerWindow::leaveFullScreen()
{
    m_slideshowLeavesFullScreen = false; // a full screen entered again later is the user's
    startGlide();
    if (m_maximizedBeforeFullScreen)
        showMaximized();
    else
        showNormal();
    presentChangedWindow();
}

void ViewerWindow::minimize()
{
    // Minimized on top of its other states, which are remembered: macOS must leave full screen to
    // minimize, and gives back a normal window (returnFromMinimized).
    m_statesBeforeMinimize = windowStates() & (Qt::WindowFullScreen | Qt::WindowMaximized);
    setWindowStates(windowStates() | Qt::WindowMinimized);
}

void ViewerWindow::returnFromMinimized()
{
    if (windowStates().testFlag(Qt::WindowMinimized)) // minimized again meanwhile: it waits for that return
        return;
    // Where the platform gave back less (macOS: a normal window), the window takes its state again;
    // m_maximizedBeforeFullScreen still says where leaving full screen goes.
    const Qt::WindowStates before = std::exchange(m_statesBeforeMinimize, Qt::WindowNoState);
    if (before.testFlag(Qt::WindowFullScreen) && !windowStates().testFlag(Qt::WindowFullScreen))
        showFullScreen();
    else if (before.testFlag(Qt::WindowMaximized) && windowStates() == Qt::WindowNoState)
        showMaximized();
    resumeOnScreen();
}

void ViewerWindow::startGlide()
{
    // Where the image is on the screen now, in global logical pixels: once the window has changed,
    // it starts there and glides to its new place (glidedImageRect()). Not on macOS, where the
    // system animates its own full screen and the window's size follows the animation.
    m_glideClock.invalidate();
    m_glidePending = false;
#if !defined(Q_OS_MACOS)
    if (m_image.width == 0 || !isExposed())
        return;
    const QRectF device = imageRect();
    const double dpr = devicePixelRatio();
    m_glideFrom = QRectF(mapToGlobal(QPointF(0, 0)) + device.topLeft() / dpr, device.size() / dpr);
    m_glidePending = true;
    QTimer::singleShot(kGlideWaitMs, this, [this] { m_glidePending = false; }); // the window did not change
#if defined(Q_OS_WIN)
    // Windows animates a window that is maximized or restored: leaving full screen, Qt gives the
    // window its normal place first and then maximizes it, which Windows drew growing from the middle
    // over a window still empty (the owner, D-114). Held back while the image glides, then given back.
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const BOOL off = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &off, sizeof(off));
    QTimer::singleShot(kGlideMs + kGlideWaitMs, this, [hwnd] {
        const BOOL on = FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &on, sizeof(on));
    });
#endif
#endif
}

void ViewerWindow::windowChanged()
{
    if (std::exchange(m_glidePending, false))
        m_glideClock.start();
}

void ViewerWindow::presentChangedWindow()
{
#if defined(Q_OS_WIN)
    // On Windows the window has its new size once show*() returns: its first frame at once, the
    // image where it was on the screen, rather than the stale one stretched until the next frame.
    if (m_rendererReady && isExposed())
        render();
#endif
}

QRectF ViewerWindow::glidedImageRect(const QRectF &target)
{
    if (!m_glideClock.isValid())
        return target;
    const double t = m_glideClock.elapsed() / double(kGlideMs);
    if (t >= 1.0) {
        m_glideClock.invalidate();
        return target;
    }
    const double dpr = devicePixelRatio();
    const QRectF from((m_glideFrom.topLeft() - mapToGlobal(QPointF(0, 0))) * dpr, m_glideFrom.size() * dpr);
    const double e = 1.0 - std::pow(1.0 - t, 3.0); // eased out: quick, then settling
    requestUpdate(); // the next step
    return QRectF(from.topLeft() + (target.topLeft() - from.topLeft()) * e, from.size() + (target.size() - from.size()) * e);
}
