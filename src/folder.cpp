#include "folder.h"

#include "formats.h"

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>
#include <vector>

QString displayFileName(const QString &name)
{
    QString shown = name;
    shown.removeIf([](QChar c) { return c.category() == QChar::Other_Control || c.category() == QChar::Other_Format; });
    return shown;
}

FolderListing listImages(const QString &directory, FolderSort sort, bool descending)
{
    static const QSet<QString> suffixes(supportedSuffixes().begin(), supportedSuffixes().end());

    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
    // Natural order ("img2" before "img10"), keyed once per file instead of per comparison.
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    struct Item {
        qint64 key; // modification time (ms) or size; 0 when sorting by name
        QCollatorSortKey name;
        QString path;
        FileStamp stamp;
    };
    std::vector<Item> items;
    items.reserve(std::size_t(entries.size()));
    for (const QFileInfo &info : entries) {
        if (info.fileName().startsWith(QLatin1String("._"))) // macOS resource-fork companions
            continue;
        if (!suffixes.contains(info.suffix().toLower()))
            continue;
        const FileStamp stamp{info.size(), info.lastModified()};
        const qint64 key = sort == FolderSort::Modified ? stamp.modified.toMSecsSinceEpoch()
                           : sort == FolderSort::Size   ? stamp.size
                                                        : 0;
        items.push_back({key, collator.sortKey(info.fileName()), info.absoluteFilePath(), stamp});
    }
    // Descending reverses the primary key only; equal keys keep the natural name order, so
    // the order is total and stable across re-listings.
    const bool reverseNames = descending && sort == FolderSort::Name;
    std::sort(items.begin(), items.end(), [descending, reverseNames](const Item &a, const Item &b) {
        if (a.key != b.key)
            return descending ? a.key > b.key : a.key < b.key;
        const int byName = a.name.compare(b.name);
        if (byName != 0)
            return reverseNames ? byName > 0 : byName < 0;
        return a.path < b.path; // names equal ignoring case: still a fixed order
    });
    FolderListing listing;
    listing.files.reserve(qsizetype(items.size()));
    listing.stamps.reserve(qsizetype(items.size()));
    for (Item &item : items) {
        listing.stamps.insert(item.path, item.stamp);
        listing.files.append(std::move(item.path));
    }
    return listing;
}
