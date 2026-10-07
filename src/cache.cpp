#include "cache.h"

#include <QFileInfo>

#include <algorithm>

qint64 ImageCache::defaultBudget()
{
    constexpr qint64 kMiB = qint64(1) << 20;
    const qint64 memory = physicalMemoryBytes();
    // Unknown memory: assume a modest machine rather than none.
    return std::clamp(memory > 0 ? memory / 4 : 1024 * kMiB, 256 * kMiB, 4096 * kMiB);
}

ImageCache::ImageCache(qint64 budget) : m_budget(budget) {}

qint64 ImageCache::bytes() const
{
    qint64 total = 0;
    for (const Entry &entry : m_entries)
        total += entry.image.pixelBytes();
    return total;
}

int ImageCache::indexOf(const QString &path) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).image.path == path)
            return i;
    return -1;
}

int ImageCache::priority(const QString &path) const
{
    return int(m_wanted.indexOf(path));
}

std::optional<Image> ImageCache::find(const QString &path, int textureLimit)
{
    const int i = indexOf(path);
    if (i < 0)
        return std::nullopt;
    const Entry &entry = m_entries.at(i);
    if (entry.state != State::Ready || entry.textureLimit != textureLimit || !entry.image.isValid())
        return std::nullopt;
    const QFileInfo info(path);
    if (!info.exists() || info.size() != entry.image.fileSize || info.lastModified() != entry.image.modified) {
        m_entries.removeAt(i);
        return std::nullopt;
    }
    return entry.image;
}

ImageCache::State ImageCache::state(const QString &path, int textureLimit) const
{
    const int i = indexOf(path);
    if (i < 0 || m_entries.at(i).textureLimit != textureLimit)
        return State::Missing;
    return m_entries.at(i).state;
}

void ImageCache::retain(const QStringList &paths)
{
    m_wanted = paths;
    m_entries.removeIf([this](const Entry &entry) { return priority(entry.image.path) < 0; });
}

void ImageCache::insert(const Image &image, int textureLimit)
{
    const int rank = priority(image.path);
    if (rank < 0)
        return;
    remove(image.path);
    Entry entry{image, textureLimit, image.overPixelLimit ? State::Skipped : State::Ready};
    // Room for it, counting only the images more important than this one.
    qint64 moreImportant = 0;
    for (const Entry &other : std::as_const(m_entries))
        if (priority(other.image.path) < rank)
            moreImportant += other.image.pixelBytes();
    if (entry.state == State::Ready && moreImportant + image.pixelBytes() > m_budget)
        entry.state = State::Skipped; // otherwise the scheduler would decode it again and again
    if (entry.state == State::Skipped) {
        entry.image.pixels.reset();
        entry.image.error.clear(); // not a failure: decoded normally once the user asks for it
    }
    // Evict less important images until this one fits.
    const qint64 size = entry.image.pixelBytes();
    while (bytes() + size > m_budget) {
        auto victim = std::max_element(m_entries.begin(), m_entries.end(), [this](const Entry &a, const Entry &b) {
            return priority(a.image.path) < priority(b.image.path);
        });
        if (victim == m_entries.end())
            break;
        m_entries.erase(victim); // always less important: the more important ones fit with this one
    }
    m_entries.append(std::move(entry));
}

void ImageCache::remove(const QString &path)
{
    m_entries.removeIf([&path](const Entry &entry) { return entry.image.path == path; });
}

void ImageCache::clear()
{
    m_entries.clear();
}
