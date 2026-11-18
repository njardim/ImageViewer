// ViewerWindow, part 8: the keyboard, the mouse and the wheel (D-08: split from viewer.cpp).
// Keys run commands (D-30); the mouse pans, clicks the side zones (D-31), the gallery (D-60) and
// the toolbars (D-65, D-112);
// the wheel zooms, or scrolls the gallery under the pointer. The side zones and the pointer's
// cursor and hiding live here too.
#include "viewer.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QStyleHints>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

void ViewerWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape && e->modifiers() == Qt::NoModifier && (m_slideshow.running() || visibility() == QWindow::FullScreen)) {
        if (m_slideshow.running()) {
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
    // Only a bar on screen takes the press: a hidden one comes back with it (pointerActive).
    const ToolbarButton button = toolbarButtonAt(e->position());
    const int barButton = mainToolbarAt(e->position());
    const bool onGallery = galleryShown() && m_gallery.contains(e->position());
    pointerActive();
    switch (e->button()) {
    case Qt::RightButton:
        showContextMenu(e->globalPosition().toPoint());
        return;
    case Qt::BackButton: // mouse side buttons: the commands, so they stop where the keys do
        run(Command::Previous);
        return;
    case Qt::ForwardButton:
        run(Command::Next);
        return;
    case Qt::LeftButton:
        break;
    default:
        return;
    }
    if (barButton != -1) {
        m_mainToolbar.setPressed(barButton); // a click when released on the same button
        QToolTip::hideText();
        m_tooltipTimer.stop();
        updateMainToolbar();
        return;
    }
    if (button != ToolbarButton::None) {
        m_toolbarPress = button; // a click when released on the same button
        updateSlideshowToolbar();
        return;
    }
    if (onGallery) { // a thumbnail shows its image; the bar itself does nothing
        if (const int slot = m_gallery.slotAt(e->position()); slot >= 0)
            galleryClicked(slot);
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
        cancelPress();
        const int barButton = mainToolbarAt(e->position());
        setMainToolbarHover(barButton);
        const ToolbarButton button = toolbarButtonAt(e->position());
        setToolbarHover(button);
        setHoverZone(button == ToolbarButton::None && barButton == -1 ? zoneAt(e->position()) : Zone::None);
        setPointerAtTop(e->position().y() >= 0 && e->position().y() < topActivationHeight());
        m_gallery.setHover(galleryShown() ? m_gallery.slotAt(e->position()) : -1);
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
    if (const int pressed = m_mainToolbar.pressed(); pressed != -1) {
        m_mainToolbar.setPressed(-1);
        updateMainToolbar();
        if (mainToolbarAt(e->position()) == pressed)
            mainToolbarClicked(pressed);
        return;
    }
    if (const ToolbarButton pressed = std::exchange(m_toolbarPress, ToolbarButton::None); pressed != ToolbarButton::None) {
        updateSlideshowToolbar();
        if (toolbarButtonAt(e->position()) == pressed)
            toolbarClicked(pressed);
        return;
    }
    m_dragging = false;
    const Zone pressed = std::exchange(m_pressZone, Zone::None);
    const Zone released = zoneAt(e->position());
    if (pressed != Zone::None && !m_pressMoved && released == pressed)
        run(pressed == Zone::Next ? Command::Next : Command::Previous);
    // Refresh unconditionally: the button loses its pressed look even when the zone is unchanged.
    m_hoverZone = released;
    updateCursor();
    updateNavigationButtons();
}

void ViewerWindow::cancelPress()
{
    // Nothing is clicked, and the pressed look goes.
    m_dragging = false;
    if (std::exchange(m_pressZone, Zone::None) != Zone::None)
        updateNavigationButtons();
    if (std::exchange(m_toolbarPress, ToolbarButton::None) != ToolbarButton::None)
        updateSlideshowToolbar();
    if (m_mainToolbar.setPressed(-1))
        updateMainToolbar();
}

void ViewerWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || (galleryShown() && m_gallery.contains(e->position())))
        return;
    if (toolbarButtonAt(e->position()) != ToolbarButton::None || mainToolbarAt(e->position()) != -1) {
        mousePressEvent(e); // quick clicks on its buttons are clicks, never full screen
        return;
    }
    if (zoneAt(e->position()) != Zone::None) {
        mousePressEvent(e); // quick clicks on a side keep navigating
        return;
    }
    // The first click may have reached the end of a folder that does not loop: the second
    // click of the pair is still a click on the side, never a request for full screen.
    if (inSideStrip(e->position()))
        return;
    run(Command::FullScreen);
}

void ViewerWindow::wheelEvent(QWheelEvent *e)
{
    // Scroll gestures (trackpads: they have phases or a touchpad device) pan; wheels and
    // Ctrl+scroll zoom. pixelDelta alone says nothing: macOS sets it for mouse wheels too.
    const bool onGallery = galleryShown() && m_gallery.contains(e->position());
    const bool onBar = toolbarButtonAt(e->position()) != ToolbarButton::None || mainToolbarAt(e->position()) != -1;
    pointerActive();
    if (onBar)
        return;
    if (onGallery) { // over the gallery the wheel scrolls the thumbnails
        if (const int steps = m_gallery.wheel(e->angleDelta().y() + e->angleDelta().x()); steps != 0)
            m_gallery.scroll(-steps);
        return;
    }
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
        zoomAt(std::pow(kZoomStep, steps), e->position() * devicePixelRatio());
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
    // Every bar that hides by itself shows again, and waits for the pointer to stay still.
    m_toolbarHide.pointerMoved();
    m_mainToolbarHide.pointerMoved();
    m_galleryHide.pointerMoved();
}

void ViewerWindow::updateCursor()
{
    const bool onButton = m_hoverZone != Zone::None || m_toolbarHover != ToolbarButton::None || m_mainToolbar.hover() != -1;
    setCursor(m_pointerHidden ? Qt::BlankCursor : onButton ? Qt::PointingHandCursor : Qt::ArrowCursor);
}
