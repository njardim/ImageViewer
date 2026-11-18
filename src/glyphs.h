// The glyphs of the toolbar's buttons (D-87, D-112): drawn by us (D-86), lines in the panels' grey
// with round caps and joins on a 24-unit square, after the owner's reference (2026-11-18). Drawn
// rather than read from files: sharp at any device pixel ratio and in HDR, and one shape for every
// state of a button (its disc says hovered, pressed or on).
#pragma once

#include <QColor>
#include <QIcon>

class QPainter;

enum class Glyph {
    // The commands' own, in the order of the command table.
    Open, ShowInFolder, OpenWith, CopyImage, CopyPath, Rename, MoveToTrash, DeletePermanently, UndoTrash,
    Settings, Quit, Previous, Next, First, Last, ZoomIn, ZoomOut, Fit, FitWidth, FitHeight, Fill, ActualSize,
    LockZoom, FullScreen, Minimize, Info, InfoOverlay, Checkerboard, Gallery, RotateClockwise,
    RotateCounterclockwise, FlipHorizontal, FlipVertical, ExposureUp, ExposureDown, ExposureReset, ToneMap,
    ClipWarning, PlayPause, PreviousFrame, NextFrame, Slideshow,
    // The slideshow's states in its button's place, and the button of what a narrow toolbar cannot hold.
    Pause, Play, Stop, More,
    // The slideshow's first design, a picture on the screen, kept at the owner's word (2026-11-18).
    SlideshowPicture,
};

// Draws `glyph` in `colour` on the 24-unit square at the painter's origin; scale the painter for
// another size. The painter's state is left as it was.
void drawGlyph(QPainter &painter, Glyph glyph, const QColor &colour);

// `glyph` as an icon for the widgets (the Settings dialog's list of buttons), at 16 to 32 pixels.
QIcon glyphIcon(Glyph glyph, const QColor &colour);
