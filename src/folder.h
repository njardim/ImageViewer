// Folder navigation: the list of viewable files next to the current image.
#pragma once

#include <QString>
#include <QStringList>

enum class FolderSort { Name, Modified, Size };

// A file name as it may be shown: no control or bidirectional-override characters, which
// could disguise it (".png" shown for a name ending in ".exe"). File operations keep the real name.
QString displayFileName(const QString &name);

// Absolute paths of the supported image files in `directory`. Names sort in natural order
// (case-insensitive, "img2" before "img10"); dates and sizes fall back to it on ties.
QStringList listImages(const QString &directory, FolderSort sort = FolderSort::Name, bool descending = false);
