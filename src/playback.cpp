// ViewerWindow, part 6 (decisions D-40, D-41): animation playback (E6) and the slideshow
// (E11). See viewer.h for the other parts.
//
// A frame is shown when its turn has come (the previous frame's time is up) and it has been
// decoded; whichever happens last shows it. One frame is decoded ahead, on its own thread,
// so a slow decode slows the animation down instead of skipping frames.
#include "viewer.h"

#include <QApplication>
#include <QLocale>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

void ViewerWindow::startAnimation()
{
    stopAnimation();
    m_animation = m_image.animation;
    if (!m_animation)
        return;
    m_frameIndex = 0;
    m_frameDurationMs = m_animation->firstDurationMs();
    m_loopsDone = 0;
    m_animationPaused = false;
    m_wantedFrame = 1;
    m_frameTimer.start(m_frameDurationMs);
    requestFrame(m_wantedFrame);
}

void ViewerWindow::stopAnimation()
{
    m_frameTimer.stop();
    ++m_frameGeneration; // a frame still being decoded is dropped when it arrives
    if (m_animation) {
        // Its other frames would stay in memory, outside the cache's budget. Trimmed on the
        // frame thread, after a frame still being decoded, so the window never waits.
        m_framePool.start([animation = m_animation] { animation->trim(); });
    }
    m_animation.reset();
    m_readyFrame.reset();
    m_frameDue = false;
    m_wantedFrame = -1;
    m_waitingForExpose = false;
}

void ViewerWindow::requestFrame(int index)
{
    if (!m_animation || m_frameBusy || index < 0)
        return; // frameDecoded() asks again for m_wantedFrame
    m_frameBusy = true;
    const std::shared_ptr<Animation> animation = m_animation;
    const int generation = m_frameGeneration;
    m_frameWatcher.setFuture(QtConcurrent::run(&m_framePool, [animation, generation, index] {
        FrameResult result;
        result.generation = generation;
        result.requested = index;
        animation->frame(index, &result.frame, &result.error);
        return result;
    }));
}

void ViewerWindow::frameDecoded()
{
    m_frameBusy = false;
    const FrameResult result = m_frameWatcher.result();
    if (result.generation != m_frameGeneration || result.requested != m_wantedFrame) {
        requestFrame(m_wantedFrame); // another animation, or another frame wanted meanwhile
        return;
    }
    if (!result.error.isEmpty()) {
        // The file went away or broke: the frame on screen stays, playback stops.
        m_frameTimer.stop();
        m_wantedFrame = -1;
        m_animationPaused = true;
        showNotice(result.error);
        return;
    }
    if (m_animation->frameCount() == 1) {
        // An "animation" of one image (an APNG, JPEG XL or AVIF sequence can be): it stays a
        // still image rather than being shown again at every tick.
        stopAnimation();
        updateOverlay();
        return;
    }
    m_readyFrame = result.frame;
    if (m_frameDue)
        presentFrame(*m_readyFrame);
}

void ViewerWindow::frameTimeout()
{
    if (!isExposed()) { // minimised or hidden: nothing to draw, nothing to decode
        m_waitingForExpose = true;
        return;
    }
    m_frameDue = true;
    if (m_readyFrame)
        presentFrame(*m_readyFrame);
}

void ViewerWindow::presentFrame(const Animation::Frame &frame)
{
    const Animation::Frame shown = frame; // `frame` may live in m_readyFrame, reset below
    m_readyFrame.reset();
    m_frameDue = false;
    // Back at the first frame: one loop done. Files that play a set number of times stop on
    // their last frame, as browsers do.
    if (shown.index == 0 && m_frameIndex != 0 && !m_animationPaused) {
        ++m_loopsDone;
        const int loops = m_animation->loopCount();
        if (loops > 0 && m_loopsDone >= loops) {
            m_animationPaused = true;
            m_wantedFrame = -1;
            updateOverlay();
            return;
        }
    }
    m_renderer.setFrame(shown.pixels, QSize(m_image.width, m_image.height));
    // The tone mapping follows the brightest frame shown so far. The peak only rises, so a
    // playing animation does not pump; the first frame's peak is the image's own.
    m_image.maxComponent = std::max(m_image.maxComponent, shown.maxComponent);
    m_image.maxLuminance = std::max(m_image.maxLuminance, shown.maxLuminance);
    m_frameIndex = shown.index;
    m_frameDurationMs = shown.durationMs;
    if (m_animationPaused) {
        m_wantedFrame = -1;
    } else {
        m_frameTimer.start(m_frameDurationMs);
        m_wantedFrame = m_frameIndex + 1; // past the last frame the animation gives the first
        requestFrame(m_wantedFrame);
    }
    if (m_showInfo)
        updateOverlay(); // the panel shows the frame number
    requestUpdate();
}

void ViewerWindow::togglePause()
{
    if (!m_animation)
        return;
    m_animationPaused = !m_animationPaused;
    if (m_animationPaused) {
        m_frameTimer.stop();
        m_frameDue = false;
        showNotice(tr("Animation paused"));
    } else {
        const int loops = m_animation->loopCount();
        if (loops > 0 && m_loopsDone >= loops) {
            // Every loop played (stopped on the last frame): start over from the first
            // frame, shown at once. Frame index 0 keeps that arrival from counting as a loop.
            m_loopsDone = 0;
            m_frameIndex = 0;
            m_readyFrame.reset();
            m_wantedFrame = 0;
            m_frameDue = true;
            requestFrame(0);
        } else {
            if (m_readyFrame && m_readyFrame->index == m_frameIndex + 1) {
                m_wantedFrame = m_readyFrame->index;
            } else {
                m_readyFrame.reset();
                m_wantedFrame = m_frameIndex + 1;
                requestFrame(m_wantedFrame);
            }
            m_frameTimer.start(m_frameDurationMs);
        }
        showNotice(tr("Animation playing"));
    }
    updateOverlay();
}

void ViewerWindow::stepFrame(int delta)
{
    if (!m_animation)
        return;
    if (!m_animationPaused) {
        m_animationPaused = true;
        m_frameTimer.stop();
    }
    int target = m_frameIndex + delta;
    if (target < 0) {
        const int count = m_animation->frameCount();
        if (count <= 0)
            return; // the last frame is not known before the animation has played once
        target = count - 1;
    }
    m_wantedFrame = target;
    m_frameDue = true; // as soon as it is decoded
    if (m_readyFrame && m_readyFrame->index == target)
        presentFrame(*m_readyFrame);
    else {
        m_readyFrame.reset();
        requestFrame(target);
    }
}

void ViewerWindow::toggleSlideshow()
{
    if (m_slideshow) {
        stopSlideshow();
        showNotice(tr("Slideshow stopped"));
        return;
    }
    if (m_files.size() < 2)
        return;
    m_slideshow = true;
    m_slideshowTimer.start(m_settings.slideshowSeconds * 1000);
    //: %1: seconds between images, e.g. "5".
    showNotice(tr("Slideshow: a new image every %1 s").arg(QLocale().toString(m_settings.slideshowSeconds)));
}

void ViewerWindow::stopSlideshow()
{
    m_slideshow = false;
    m_slideshowTimer.stop();
}

void ViewerWindow::slideshowTimeout()
{
    // Not behind a dialog or a menu: the image they act on must stay the one on screen.
    if (QGuiApplication::modalWindow() || QApplication::activePopupWidget()) {
        m_slideshowTimer.start(m_settings.slideshowSeconds * 1000);
        return;
    }
    if (!hasNeighbour(+1)) { // the end of a folder that does not loop
        stopSlideshow();
        showNotice(tr("Slideshow stopped"));
        return;
    }
    step(+1);
}
