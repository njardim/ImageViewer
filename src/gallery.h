// The gallery (D-60): the folder's images as thumbnails in a bar at the bottom of the window. Its
// layout, its thumbnails (decoded on their own threads, kept by the folder listing's stamps), and
// the texture the renderer composites at SDR white. The window owns one, gives it what it shows
// (Scene) and the settings and files it reads, lays it out again when it says it changed, and
// navigates when a thumbnail is clicked; the decoding itself is handed in (Decode), so that the
// class needs nothing of the decoders and the unit tests drive it with their own
// (tests/unit_tests.cpp, D-102).
#pragma once

#include "folder.h"
#include "settings.h"

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPainterPath>
#include <QRectF>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QStringList>
#include <QThreadPool>

#include <functional>

// A gallery thumbnail: sRGB, 8 bits, premultiplied; null when the file is unreadable. The file's
// size and time say when it was written again since.
struct Thumbnail {
    QImage image;
    qint64 fileSize = -1;
    QDateTime modified;
    qreal devicePixelRatio = 1.0; // the screen's it was decoded for: a sharper screen asks for it again
};

class Gallery : public QObject {
    Q_OBJECT

public:
    // `path` decoded to an sRGB image (QImage::Format_RGBA8888_Premultiplied) whose longer side is
    // at most `pixels`, within `maxPixels` (0: none); null when it cannot be. Runs on the gallery's
    // own threads.
    using Decode = std::function<QImage(const QString &path, int pixels, qint64 maxPixels)>;

    // What the window shows: its logical size and pixel ratio, the current file, the preload's
    // pixel limit (a huge image is decoded whole before it is reduced).
    struct Scene {
        QSizeF window;
        qreal devicePixelRatio = 1.0;
        int current = -1; // index in the files; -1: none
        qint64 maxPixels = 0;
    };

    // `settings` and `files` are the window's, read at every update; they outlive the gallery.
    Gallery(const Settings &settings, const QStringList &files, Decode decode, QObject *parent = nullptr);
    ~Gallery() override; // thumbnails not started are not decoded, the running ones are waited for

    // Layout, logical pixels.
    bool wanted() const;              // on in the settings, with files to show
    double thumbnailSide() const;     // a thumbnail's side
    int zoom() const;                 // added to the thumbnail under the pointer
    double overhang() const;          // how far its tile reaches beyond the bar, above and below
    double bottomMargin() const;      // below the bar
    double reserve() const;           // what the gallery takes from the window's bottom; 0: hidden
    int capacity(double windowWidth) const; // thumbnails that fit the window's width
    int fileAt(int slot) const;       // the index in the files shown in a slot; -1: none
    bool visible() const { return !m_size.isEmpty(); } // its texture is shown
    QRectF rect() const { return m_rect; }    // device pixels, where its texture goes
    bool contains(const QPointF &position) const; // over its background (logical pixels)
    int slotAt(const QPointF &position) const;    // the slot under `position`; -1: none
    int hover() const { return m_hover; }

    // Lays the gallery out for `scene`, asks for the thumbnails it lacks and draws its texture
    // when what it shows changed (textureChanged()). Call at every change of the window, the
    // files, the current image or the settings, and when it says it changed (changed()).
    void update(const Scene &scene);
    void hide();                      // the texture goes (textureChanged() with a null image)
    void invalidate();                // drawn again at the next update (the texture was lost)
    void setHover(int slot);          // the slot under the pointer; -1: none
    void scroll(int steps);           // the wheel: away from the current image
    int wheel(int angleDelta);        // accumulates the wheel's 1/8 degrees; whole steps turned
    void recentre();                  // the current image to the middle at the next update

    // After a folder listing: the thumbnails of files not in it, or written again since, go; one
    // being decoded is checked against these stamps when it arrives.
    void forgetChanged(const QHash<QString, FileStamp> &stamps);
    void rename(const QString &path, const QString &target); // the same file, renamed: its thumbnail stays

    // For the tests: decoded and waiting, and the thumbnail kept for `path` (none: null image).
    bool idle() const { return m_pending.isEmpty() && m_queue.isEmpty(); }
    bool has(const QString &path) const { return m_thumbnails.contains(path); }
    void stop(); // the window closes: thumbnails not started are not decoded, the running ones are waited for

Q_SIGNALS:
    // Lay it out again (update()): a thumbnail arrived, the hover or the scroll changed.
    void changed();
    // The texture to show at rect(); a null image: none.
    void textureChanged(const QImage &image);

private:
    void startThumbnails();
    void decodeThumbnail(const QString &path);
    void arrived(const QString &path, const Thumbnail &thumbnail);
    void keep(const QString &path, const Thumbnail &thumbnail);

    const Settings &m_settings;
    const QStringList &m_files;
    Decode m_decode;
    Scene m_scene;                       // the last update's
    QHash<QString, Thumbnail> m_thumbnails; // by path
    QSet<QString> m_pending;             // being decoded
    QStringList m_queue;                 // to decode next, most wanted first
    QHash<QString, FileStamp> m_listing; // the latest listing's: a thumbnail read from other content is not kept
    QHash<QString, FileStamp> m_retried; // ...but taken when read twice from the same (the listing was the stale one)
    QList<QRectF> m_slots;               // logical pixels in the texture
    QPainterPath m_shape;                // likewise: its background, where the pointer is over the gallery
    QRectF m_rect;                       // device pixels
    QSize m_size;                        // device pixels; empty: hidden
    QString m_key;                       // what the texture shows
    int m_first = 0;                     // the file shown in the first slot
    int m_hover = -1;                    // slot under the pointer
    int m_centredOn = -2;                // the file the gallery last centred
    int m_wheel = 0;                     // wheel angle not yet turned into a step (1/8 degrees)
    QThreadPool m_pool;                  // two threads
};
