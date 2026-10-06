// Folder navigation: the list of viewable files next to the current image.
#pragma once

#include <QString>
#include <QStringList>

// Absolute paths of the supported, non-hidden image files in `directory`,
// in natural order (case-insensitive, "img2" before "img10").
QStringList listImages(const QString &directory);
