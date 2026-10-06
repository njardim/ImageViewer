#include "folder.h"

#include "image.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

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

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&collator](const QString &a, const QString &b) {
        return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
    return files;
}
