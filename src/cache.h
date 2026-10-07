// Decoded images kept in memory so that a step to the next or previous image skips the
// decode (decision D-33). Holds what the GPU receives (linear scRGB RGBA16F); the pixel
// buffers are shared with the renderer, never copied.
#pragma once

#include "image.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

class ImageCache {
public:
    enum class State { Missing, Ready, Skipped };

    // A quarter of physical memory, at least 256 MiB and at most 4 GiB.
    static qint64 defaultBudget();
    explicit ImageCache(qint64 budget = defaultBudget());

    qint64 budget() const { return m_budget; }
    qint64 bytes() const; // pixel bytes held
    // Largest source (width × height) worth preloading: a third of the budget at 8 bytes per pixel.
    qint64 maxPreloadPixels() const { return m_budget / 3 / 8; }

    // The image decoded for `path` at `textureLimit`, if the file has not changed since
    // (size and modification time); a stale entry is dropped. Failed decodes are returned
    // too (their error is the answer); images skipped as too large are not.
    std::optional<Image> find(const QString &path, int textureLimit);
    // Ready or Skipped entries need no further preloading; the file is not checked.
    State state(const QString &path, int textureLimit) const;

    // Paths worth keeping, most important first (current image, then its neighbours).
    // Everything else is dropped now; inserts beyond the budget evict from the end.
    void retain(const QStringList &paths);
    // Stores a finished decode (also a failed one, or one over the preload limit) for one of
    // the retained paths; anything else, or an image that cannot fit, is not kept.
    void insert(const Image &image, int textureLimit);
    void remove(const QString &path);
    void clear();

private:
    struct Entry {
        Image image;
        int textureLimit = 0;
        State state = State::Ready;
    };
    int priority(const QString &path) const; // index in the retained list, -1 if absent
    int indexOf(const QString &path) const;

    qint64 m_budget;
    QStringList m_wanted;
    QList<Entry> m_entries; // at most a handful: linear search is cheapest
};
