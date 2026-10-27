// The interface's icons (decision D-86): ours, the same on Windows, macOS and Linux, inside each
// system's own widgets, windows and dialogs; and the user's font size (D-85).
#pragma once

#include "settings.h"

#include <QColor>
#include <QIcon>
#include <QMessageBox>

// The system's own widget style with our icons in it, and the user's font size. Called once at
// startup, before any widget exists.
void applyAppearance(FontSize size);
// The font size alone, when the user changes it.
void applyFontSize(FontSize size);

// The interface's text against the system's own size: 1 at Small, more at Medium and Large. For
// what is sized in pixels around text (the About dialog's text column).
qreal fontScale();

// Our sign for a message box's `icon`, 64 logical pixels, in the system's own box.
void setMessageIcon(QMessageBox &box, QMessageBox::Icon icon);

// A magnifying glass in `colour`, for search fields.
QIcon searchIcon(const QColor &colour);
