// Folder navigation: the list of viewable files next to the current image.
#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

enum class FolderSort { Name, Modified, Size };

// A file's size and modification time, as read when it was listed.
struct FileStamp {
    qint64 size = -1;
    QDateTime modified;
};

// A folder's supported images, sorted, and each one's stamp: what the gallery compares its
// thumbnails with, so that the interface's thread never asks the disk (a network share answers
// in milliseconds).
struct FolderListing {
    QStringList files;
    QHash<QString, FileStamp> stamps; // by path
};

// A file name as it may be shown: no control or bidirectional-override characters, which
// could disguise it (".png" shown for a name ending in ".exe"). File operations keep the real name.
QString displayFileName(const QString &name);

// The supported image files in `directory`, as absolute paths. Names sort in natural order
// (case-insensitive, "img2" before "img10"); dates and sizes fall back to it on ties.
FolderListing listImages(const QString &directory, FolderSort sort = FolderSort::Name, bool descending = false);
