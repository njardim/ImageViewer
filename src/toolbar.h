// The toolbar (D-87, D-112): a bar of buttons over the image, at the top of the window, with our
// glyphs (glyphs.h) on a panel in the information panels' look. Separators between groups;
// optionally each button's name under it; the buttons a narrow window cannot hold under a "…"
// button at the end, which opens them as a panel of rows below it, in the bar's look (D-113). It
// lays itself out for a width, says which button is under the pointer (the buttons' areas touch,
// so that no point of the bar is between two), keeps the hover and the press, and draws the
// textures the renderer composites. The window gives it the buttons (each its
// command's glyph, name and state) and runs the command a click gives (D-85), so the unit tests
// drive it alone (tests/unit_tests.cpp, D-102).
//
// AutoHide hides a bar while the pointer stays still (D-65): the slideshow's toolbar, this one and
// the gallery each have one.
#pragma once

#include "glyphs.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

class QPainter;

class Toolbar {
public:
    // A button, or a separator between groups.
    struct Button {
        int id = -1;          // the window's: the command it runs
        Glyph glyph = Glyph::More;
        QString name;         // under the glyph when the names show, and in the "…" menu
        bool enabled = true;
        bool on = false;      // a switch that is on: Look::on behind its glyph
        bool separator = false;
        QString shortcut;     // its key as the menus show it, in the "…" panel; empty: none
        bool operator==(const Button &) const = default;
    };
    struct Look {
        qreal devicePixelRatio = 1.0;
        bool names = false;   // each button's name under it
        QColor panel;         // the panel behind the buttons (Settings::panelBackground())
        QColor ink;           // the glyphs and names (the panels' text, at its opacity)
        QColor on;            // behind a switch that is on, or a pressed button (Settings::highlightTile())
        QColor hover;         // behind the button under the pointer
        QFont font;           // the names'
        QFont menuFont;       // the "…" panel's rows
        double radius = 8.0;  // the panel's corners (kPanelRadius)
        bool operator==(const Look &) const = default;
    };
    static constexpr int kMore = -2; // the "…" button's id
    static constexpr double kButton = 40.0;   // a button's square, logical pixels
    static constexpr double kGlyph = 24.0;    // its glyph
    static constexpr double kDisc = 36.0;     // the disc behind a hovered, pressed or switched-on glyph
    static constexpr double kPadding = 6.0;   // inside the panel, around the buttons
    static constexpr double kSpacing = 2.0;   // between buttons
    static constexpr double kSeparator = 13.0; // a separator's slot
    static constexpr double kRow = 32.0;       // a row of the "…" panel

    // Both return whether the bar changed (it is laid out and drawn again).
    bool setButtons(const QList<Button> &buttons);
    bool setLook(const Look &look);
    // Lays the bar out for at most `width` logical pixels: the buttons that do not fit go, from the
    // end, under "…"; separators at either end of what shows are left out.
    void layout(double width);

    QSizeF size() const { return m_size; }  // logical pixels; empty when there is no button
    QList<Button> overflow() const;          // the buttons under "…", in order, without separators
    // The button under `position` (logical pixels from the bar's top left): its id, kMore, or -1
    // outside the bar. A point between two buttons belongs to the one at its right.
    int buttonAt(const QPointF &position) const;
    QRectF buttonRect(int id) const;          // a shown button's square, logical pixels in the bar
    const Button *button(int id) const;       // a shown button (or kMore's); null when not shown

    bool setHover(int id);   // -1: none; whether it changed
    bool setPressed(int id); // likewise
    int hover() const { return m_hover; }
    int pressed() const { return m_pressed; }

    // The bar at the look's pixel ratio, drawn again only when something it shows changed.
    QImage texture();

    // The "…" panel: the buttons under "…" as rows, each its glyph, name and key. It closes when
    // nothing is left under "…".
    bool setMenuOpen(bool open); // whether it changed
    bool menuOpen() const { return m_menuOpen && !m_menuSize.isEmpty(); }
    QSizeF menuSize() const { return menuOpen() ? m_menuSize : QSizeF(); } // logical pixels
    int menuRowAt(const QPointF &position) const; // a row's id (logical pixels in the panel), or -1
    bool setMenuHover(int id);
    int menuHover() const { return m_menuHover; }
    QImage menuTexture(); // drawn again only on a change; null when closed

private:
    struct Slot {
        Button button;
        double left = 0;  // the button's square, logical x in the bar
        double width = 0; // its column: the square, or its name's width when wider
        double from = 0;  // the area that takes the pointer, from...
        double to = 0;    // ...to (exclusive)
    };
    double columnWidth(const Button &button) const;
    double nameHeight() const;
    QStringList nameLines(const QString &name, double width) const; // at most two, the second elided
    void drawButton(QPainter &painter, const Slot &slot) const;
    void layoutMenu();

    QList<Button> m_buttons;
    Look m_look;
    double m_width = -1;   // the width laid out for
    QList<Slot> m_slots;   // what shows: buttons, separators, and "…" last when some do not fit
    int m_shownButtons = 0; // how many of m_buttons show (the rest are under "…")
    QSizeF m_size;
    int m_hover = -1;
    int m_pressed = -1;
    QImage m_texture;
    bool m_dirty = true;
    bool m_menuOpen = false;
    QSizeF m_menuSize;       // logical pixels, for what is under "…"
    double m_menuNameWidth = 0;  // the widest name
    int m_menuHover = -1;
    QImage m_menuTexture;
    bool m_menuDirty = true;
};

class AutoHide : public QObject {
    Q_OBJECT

public:
    // `underPointer` says whether the pointer is over the bar, which never hides from under it.
    explicit AutoHide(std::function<bool()> underPointer, QObject *parent = nullptr);

    // On, the bar hides after `delayMs` of a still pointer; off, it shows. Either way it shows now.
    void configure(bool enabled, int delayMs);
    void pointerMoved(); // shows the bar and starts the wait again
    void show();         // shown, and the wait starts again when enabled
    bool hidden() const { return m_hidden; }
    bool enabled() const { return m_enabled; }

Q_SIGNALS:
    void hiddenChanged(bool hidden);

private:
    void timeout();

    std::function<bool()> m_underPointer;
    QTimer m_timer;
    bool m_enabled = false;
    bool m_hidden = false;
};
