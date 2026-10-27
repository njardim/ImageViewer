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

// Our sign for a message box's `icon`, in the system's own box.
void setMessageIcon(QMessageBox &box, QMessageBox::Icon icon);

// A magnifying glass in `colour`, for search fields.
QIcon searchIcon(const QColor &colour);
