#include "folder.h"

#include "image.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>
#include <vector>

QStringList listImages(const QString &directory)
{
    static const QSet<QString> suffixes(supportedSuffixes().begin(), supportedSuffixes().end());

    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
    QStringList files;
    files.reserve(entries.size());
    for (const QFileInfo &info : entries) {
        if (info.fileName().startsWith(QLatin1String("._"))) // macOS resource-fork companions
            continue;
        if (suffixes.contains(info.suffix().toLower()))
            files.append(info.absoluteFilePath());
    }

    // Natural order ("img2" before "img10"), keyed once per file instead of per comparison.
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::vector<std::pair<QCollatorSortKey, QString>> keyed;
    keyed.reserve(std::size_t(files.size()));
    for (QString &file : files)
        keyed.emplace_back(collator.sortKey(QFileInfo(file).fileName()), std::move(file));
    std::sort(keyed.begin(), keyed.end(), [](const auto &a, const auto &b) { return a.first.compare(b.first) < 0; });
    files.clear();
    for (auto &entry : keyed)
        files.append(std::move(entry.second));
    return files;
}
