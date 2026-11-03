// The slideshow's clock (E11): when the next image is due. Each image is shown a full interval
// from its arrival (D-100), never while the window is off the screen (D-84) nor behind a dialog or
// a menu, and an image slower to decode than the interval is waited for, never skipped (D-100).
// The window decides what a step is and whether there is one (the conditions it gives next());
// its notices, its full screen and the slideshow's toolbar stay the window's. A class of its own
// since D-102 (it was part of ViewerWindow), tested alone (tests/unit_tests.cpp).
#pragma once

#include <QObject>
#include <QTimer>

class Slideshow : public QObject {
    Q_OBJECT

public:
    explicit Slideshow(QObject *parent = nullptr);

    bool running() const { return m_running; }
    bool paused() const { return m_paused; }
    bool counting() const { return m_timer.isActive(); } // an interval is running
    int remainingMs() const { return m_timer.remainingTime(); }
    bool waitsForScreen() const { return m_waitsForScreen; }

    void start(int intervalMs);   // from the image on screen
    void stop();
    bool togglePause();           // true: now paused
    void setInterval(int ms);     // a running one takes it at once
    void restart();               // a new image: a full interval from now (not while paused)
    void resumeOnScreen();        // the window is seen again: the image shown gets its full time

    // What the window knows when an interval ends.
    struct Conditions {
        bool onScreen = true;     // exposed and not minimized
        bool behindDialog = false; // a modal window or a menu is open
        bool awaiting = false;    // the image asked for has not arrived yet
        bool hasNext = true;      // a next image (the folder loops, or this is not the last)
    };
    enum class Action {
        None, // waits: off the screen, behind a dialog (another interval), or for the image asked for
        Step, // show the next image
        End,  // after the last image of a folder that does not loop: stop, and say so
    };
    // Called by the window on due(): what to do now, and the clock set for what follows.
    Action next(const Conditions &conditions);

Q_SIGNALS:
    void due(); // an interval ended: the window calls next()

private:
    QTimer m_timer;
    bool m_running = false;
    bool m_paused = false;
    bool m_waitsForScreen = false; // its interval stopped while the window was off the screen
};
