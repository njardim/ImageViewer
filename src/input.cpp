// ViewerWindow, part 8: the keyboard, the mouse and the wheel (D-08: split from viewer.cpp).
// Keys run commands (D-30); the mouse pans, clicks the side zones (D-31) and the gallery (D-60);
// the wheel zooms, or scrolls the gallery under the pointer.
#include "viewer.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QStyleHints>
#include <QWheelEvent>

#include <cmath>

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
    if (galleryContains(e->position())) { // a thumbnail shows its image; the bar itself does nothing
        if (const int slot = galleryIndexAt(e->position()); slot >= 0)
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
        m_dragging = false;
        m_pressZone = Zone::None;
        setHoverZone(zoneAt(e->position()));
        setPointerAtTop(e->position().y() >= 0 && e->position().y() < topActivationHeight());
        setGalleryHover(galleryIndexAt(e->position()));
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
    if (e->button() != Qt::LeftButton || galleryContains(e->position()))
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
    if (galleryContains(e->position())) { // over the gallery the wheel scrolls the thumbnails
        m_galleryWheel += e->angleDelta().y() + e->angleDelta().x();
        if (const int steps = m_galleryWheel / 120; steps != 0) {
            m_galleryWheel -= steps * 120;
            scrollGallery(-steps);
        }
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
        zoomAt(std::pow(1.25, steps), e->position() * devicePixelRatio());
}
