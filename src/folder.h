// Folder navigation: the list of viewable files next to the current image.
#pragma once

#include <QString>
#include <QStringList>

enum class FolderSort { Name, Modified, Size };

// Absolute paths of the supported image files in `directory`. Names sort in natural order
// (case-insensitive, "img2" before "img10"); dates and sizes fall back to it on ties.
QStringList listImages(const QString &directory, FolderSort sort = FolderSort::Name, bool descending = false);
