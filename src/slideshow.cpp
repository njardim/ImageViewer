// The slideshow's clock (E11, D-84, D-100): see slideshow.h.
#include "slideshow.h"

#include <utility>

Slideshow::Slideshow(QObject *parent) : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &Slideshow::due);
}

void Slideshow::start(int intervalMs)
{
    m_running = true;
    m_paused = false;
    m_waitsForScreen = false;
    m_timer.start(intervalMs);
}

void Slideshow::stop()
{
    m_running = false;
    m_paused = false;
    m_waitsForScreen = false;
    m_timer.stop();
}

bool Slideshow::togglePause()
{
    m_paused = !m_paused;
    if (m_paused)
        m_timer.stop();
    else
        m_timer.start(); // the image on screen gets a full interval
    return m_paused;
}

void Slideshow::setInterval(int ms)
{
    m_timer.setInterval(ms);
    if (m_running && !m_paused && m_timer.isActive())
        m_timer.start(); // a running slideshow takes the new interval
}

void Slideshow::restart()
{
    if (m_running && !m_paused)
        m_timer.start();
}

void Slideshow::resumeOnScreen()
{
    if (std::exchange(m_waitsForScreen, false) && m_running && !m_paused)
        m_timer.start();
}

Slideshow::Action Slideshow::next(const Conditions &conditions)
{
    if (!m_running || m_paused)
        return Action::None;
    // Not while the window is off the screen (minimized, for one): nobody sees the images it would
    // decode. It waits for the window to come back, and the image shown then gets its full time
    // (resumeOnScreen()).
    if (!conditions.onScreen) {
        m_timer.stop();
        m_waitsForScreen = true;
        return Action::None;
    }
    // Not behind a dialog or a menu: the image they act on must stay the one on screen.
    if (conditions.behindDialog) {
        m_timer.start();
        return Action::None;
    }
    // The image asked for has not arrived yet (a decode slower than the interval): the slideshow waits
    // for it rather than skip it, and it is shown a full interval from its arrival (restart()).
    if (conditions.awaiting)
        return Action::None;
    // After the last image, the first only when navigation loops (D-61).
    if (!conditions.hasNext)
        return Action::End;
    return Action::Step;
}
