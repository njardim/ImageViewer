// The interface's look (decision D-86): every component drawn by the application, the same on
// Windows, macOS and Linux, rather than left to each system.
#pragma once

#include "settings.h"

#include <QColor>
#include <QIcon>

// One widget style with our palette, light or dark as the system's colour scheme (and again
// whenever it changes), and the user's font size (D-85). Called once at startup, before any
// widget exists.
void applyAppearance(FontSize size);
// The font size alone, when the user changes it.
void applyFontSize(FontSize size);

// A magnifying glass in `colour`, for search fields.
QIcon searchIcon(const QColor &colour);
