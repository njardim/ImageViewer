// The gallery (D-60): the folder's images as thumbnails in a bar at the bottom. The one under the
// pointer enlarges (the zoom), a click shows that image (the window's navigation), the wheel
// scrolls. Thumbnails are decoded on their own threads, colour-managed to sRGB like the clipboard,
// and drawn into one texture the renderer composites at SDR white, like the information panels.
// A class of its own since D-102 (it was part of ViewerWindow).
#include "gallery.h"

#include "viewer.h" // kEdgeMargin, kPanelRadius

#include <QFileInfo>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kBorder = 8.0;       // between thumbnails and around them
constexpr double kSideFree = 128.0;   // left free on each side of the window
constexpr double kTileMargin = 3.0;   // the tiles reach this far around a thumbnail
constexpr int kHoverFade = 20;        // the hovered tile: the highlight 20 points less opaque, 50 % by default (D-85)
constexpr int kPrefetch = 1;          // thumbnails decoded ahead: this many rows' worth either side
} // namespace

Gallery::Gallery(const Settings &settings, const QStringList &files, Decode decode, QObject *parent)
    : QObject(parent), m_settings(settings), m_files(files), m_decode(std::move(decode))
{
    m_pool.setMaxThreadCount(2); // the gallery's thumbnails never hold up the image shown
}

Gallery::~Gallery()
{
    stop();
}

void Gallery::stop()
{
    m_queue.clear();
    m_pool.clear();
    m_pool.waitForDone();
}

bool Gallery::wanted() const
{
    return m_settings.showGallery && !m_files.isEmpty();
}

double Gallery::thumbnailSide() const
{
    return m_settings.galleryThumbnailSize; // square
}

int Gallery::zoom() const
{
    return m_settings.galleryZoom ? m_settings.galleryEnlargement : 0;
}

double Gallery::overhang() const
{
    // The thumbnail under the pointer grows by the zoom about the bar's middle line; its tile, and
    // the bar's background behind it, reach this far above and below the bar.
    return std::max(0.0, zoom() / 2.0 + kTileMargin - kBorder);
}

int Gallery::capacity(double windowWidth) const
{
    // n thumbnails of side t take n·t + (n + 1)·8 px, the zoom widens the row by its own size, and 128 px
    // stay free on either side.
    const double room = windowWidth - 2 * kSideFree - kBorder - zoom();
    return std::max(1, int(std::floor(room / (thumbnailSide() + kBorder))));
}

double Gallery::bottomMargin() const
{
    // From the window's bottom to the bar: kEdgeMargin, or more when the enlarged thumbnail's tile
    // reaches further below the bar; it then touches the bottom (D-61).
    return std::max(kEdgeMargin, overhang());
}

double Gallery::reserve() const
{
    // The bar with the tile's reach above and below it, and its margin: the information panel
    // stays above all of it.
    return m_size.isEmpty() ? 0.0 : bottomMargin() + thumbnailSide() + 2 * kBorder + overhang();
}

int Gallery::fileAt(int slot) const
{
    // Navigation's loop makes the gallery a ring: after the last image, the first again.
    const int count = int(m_files.size());
    const int file = m_first + slot;
    if (m_settings.loop && count > 0)
        return (file % count + count) % count;
    return file >= 0 && file < count ? file : -1;
}

void Gallery::hide()
{
    m_queue.clear();
    if (m_size.isEmpty())
        return;
    m_size = QSize();
    m_key.clear(); // drawn again when it shows again
    m_slots.clear();
    m_hover = -1;
    Q_EMIT textureChanged(QImage());
}

void Gallery::update(const Scene &scene)
{
    m_scene = scene;
    if (!wanted()) {
        hide();
        return;
    }
    const int count = int(m_files.size());
    const double width = scene.window.width(), height = scene.window.height();
    // When not every image fits, an odd number: the current thumbnail then sits in the middle.
    const int capacity = this->capacity(width);
    const int shown = count <= capacity ? count : std::max(1, capacity % 2 ? capacity : capacity - 1);
    // The current image comes to the centre whenever it changes; the wheel moves away from it.
    // When the window holds more or fewer thumbnails, the middle one stays (both counts are odd).
    if (!m_slots.isEmpty())
        m_first += (int(m_slots.size()) - shown) / 2;
    if (scene.current != m_centredOn) {
        m_centredOn = scene.current;
        m_first = std::max(0, scene.current) - shown / 2;
    }
    m_first = m_settings.loop && count > shown ? fileAt(0) : std::clamp(m_first, 0, count - shown);
    if (m_hover >= shown)
        m_hover = -1;

    // Thumbnails for what is shown first, then a row's worth either side. Asked again at every
    // change: what scrolled out of reach before its turn is never decoded (the pool's own queue
    // would decode it before what is shown now).
    QStringList wanted;
    for (int i = 0; i < shown; ++i)
        wanted << m_files.at(fileAt(i));
    for (int i = -kPrefetch * shown; i < (kPrefetch + 1) * shown; ++i)
        if (const int file = fileAt(i); file >= 0)
            wanted << m_files.at(file);
    // Missing, or decoded for a screen less sharp than this one (the window moved to a 2x screen).
    const double dpr = scene.devicePixelRatio;
    const auto needed = [this, dpr](const QString &path) {
        const auto thumbnail = m_thumbnails.constFind(path);
        return thumbnail == m_thumbnails.cend() || (!thumbnail->image.isNull() && thumbnail->devicePixelRatio < dpr);
    };
    m_queue.clear();
    for (const QString &path : std::as_const(wanted))
        if (needed(path) && !m_pending.contains(path) && !m_queue.contains(path))
            m_queue << path;
    startThumbnails();

    const int zoom = this->zoom();
    const double overhang = this->overhang();
    const double side = thumbnailSide();
    const double barHeight = side + 2 * kBorder;
    const QSizeF logical(shown * (side + kBorder) + kBorder + zoom, barHeight + 2 * overhang);

    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|")
                      .arg(m_first).arg(shown).arg(m_hover).arg(scene.current).arg(dpr)
                      .arg(m_settings.galleryOpacity).arg(zoom).arg(side)
                  + m_settings.galleryHighlight.name(QColor::HexRgb);
    for (int i = 0; i < shown; ++i) { // which file, and its thumbnail's sharpness, if it is there yet
        const QString &path = m_files.at(fileAt(i));
        const auto thumbnail = m_thumbnails.constFind(path);
        key += (thumbnail == m_thumbnails.cend() ? QStringLiteral("-") : QString::number(thumbnail->devicePixelRatio))
               + QLatin1Char(':') + QString::number(qHash(path), 36);
    }
    m_rect = QRectF(QPointF(std::round((width - logical.width()) / 2.0 * dpr),
                            std::round((height - bottomMargin() - barHeight - overhang) * dpr)),
                    logical * dpr);
    if (key == m_key)
        return;
    m_key = key;

    // The places: the thumbnail under the pointer is larger by the zoom, the others move aside.
    const double rowWidth = shown * (side + kBorder) + kBorder + (m_hover >= 0 ? zoom : 0);
    const double middle = overhang + barHeight / 2.0; // thumbnails grow evenly about it
    QList<QRectF> places;
    double x = (logical.width() - rowWidth) / 2.0 + kBorder;
    for (int i = 0; i < shown; ++i) {
        const double size = side + (i == m_hover ? zoom : 0);
        places << QRectF(x, middle - size / 2.0, size, size);
        x += size + kBorder;
    }
    const auto tileOf = [](const QRectF &place) {
        return place.adjusted(-kTileMargin, -kTileMargin, kTileMargin, kTileMargin);
    };

    QImage image(QSize(int(std::ceil(logical.width() * dpr)), int(std::ceil(logical.height() * dpr))),
                 QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // The background: the bar, and around the enlarged thumbnail exactly its tile, in one path
    // filled once (two translucent shapes would darken where they overlap).
    QPainterPath background;
    background.addRoundedRect(QRectF((logical.width() - rowWidth) / 2.0, overhang, rowWidth, barHeight),
                              kPanelRadius, kPanelRadius);
    if (m_hover >= 0) {
        QPainterPath tile;
        tile.addRoundedRect(tileOf(places.at(m_hover)), kPanelRadius / 2, kPanelRadius / 2);
        background = background.united(tile);
    }
    const int alpha = int(std::lround(m_settings.galleryOpacity * 2.55));
    painter.setPen(Qt::NoPen);
    painter.fillPath(background, QColor(0, 0, 0, alpha));
    m_shape = background;

    // The pointer is matched against the places without the zoom: matched against the enlarged
    // one, a thumbnail growing under the pointer pushed it onto its neighbour, which grew in turn,
    // and the hover flickered between them. Each place takes half the gap either side.
    m_slots.clear();
    for (int i = 0; i < shown; ++i)
        m_slots << QRectF((zoom + kBorder) / 2.0 + i * (side + kBorder), 0, side + kBorder, logical.height());

    for (int i = 0; i < shown; ++i) {
        const QRectF &place = places.at(i);
        const QString &path = m_files.at(fileAt(i));
        // One colour behind the current thumbnail and the hovered one, at the gallery's opacity
        // over its background; the hovered one a little fainter, unless it is the current one.
        if (fileAt(i) == scene.current || i == m_hover) {
            QColor tile = m_settings.galleryHighlight;
            const int opacity = fileAt(i) == scene.current ? m_settings.galleryOpacity
                                                           : std::max(0, m_settings.galleryOpacity - kHoverFade);
            tile.setAlpha(int(std::lround(opacity * 2.55)));
            painter.setBrush(tile);
            painter.drawRoundedRect(tileOf(place), kPanelRadius / 2, kPanelRadius / 2);
        }
        const QImage thumbnail = m_thumbnails.value(path).image;
        if (thumbnail.isNull()) {
            painter.setBrush(QColor(255, 255, 255, 28)); // still being decoded, or unreadable
            painter.drawRoundedRect(place, kPanelRadius / 2, kPanelRadius / 2);
        } else {
            const QSizeF fitted = QSizeF(thumbnail.size()).scaled(place.size(), Qt::KeepAspectRatio);
            painter.drawImage(QRectF(place.center() - QPointF(fitted.width(), fitted.height()) / 2.0, fitted), thumbnail);
        }
    }
    painter.end();
    m_size = image.size();
    Q_EMIT textureChanged(image);
}

void Gallery::invalidate()
{
    m_key.clear();
}

void Gallery::startThumbnails()
{
    while (!m_queue.isEmpty() && m_pending.size() < m_pool.maxThreadCount())
        decodeThumbnail(m_queue.takeFirst());
}

void Gallery::decodeThumbnail(const QString &path)
{
    m_pending.insert(path);
    // Decoded at the largest size the zoom can show with the largest thumbnails, reduced in linear
    // light: another thumbnail size then decodes nothing again.
    const qreal dpr = m_scene.devicePixelRatio;
    const int pixels = int(std::ceil((Settings::kGalleryThumbnailSizes.back() + Settings::kMaxGalleryEnlargement) * dpr));
    const qint64 maxPixels = m_scene.maxPixels;
    m_pool.start([this, decode = m_decode, path, pixels, dpr, maxPixels] {
        Thumbnail thumbnail;
        thumbnail.devicePixelRatio = dpr;
        const QFileInfo info(path); // before decoding: a file changed meanwhile is decoded again
        thumbnail.fileSize = info.size();
        thumbnail.modified = info.lastModified();
        thumbnail.image = decode(path, pixels, maxPixels);
        QMetaObject::invokeMethod(this, [this, path, thumbnail] { arrived(path, thumbnail); }, Qt::QueuedConnection);
    });
}

void Gallery::arrived(const QString &path, const Thumbnail &thumbnail)
{
    m_pending.remove(path);
    // Kept when it was read from the content the latest listing saw, file by file: a listing made
    // while it was decoded drops only the thumbnails of the files it found changed (a folder that
    // keeps changing, a download beside the images, never holds the others back). Read from other
    // content, it is asked for again; read twice from the same, it is kept (the listing was the stale
    // one, as on a share that reports no change). A file no longer listed is not kept.
    const auto same = [&thumbnail](const FileStamp &stamp) {
        return stamp.size == thumbnail.fileSize && stamp.modified == thumbnail.modified;
    };
    const auto listed = m_listing.constFind(path);
    const auto retried = m_retried.constFind(path);
    if (listed == m_listing.cend() ? m_files.contains(path)
                                   : same(*listed) || (retried != m_retried.cend() && same(*retried))) {
        m_retried.remove(path);
        keep(path, thumbnail);
        return;
    }
    if (listed != m_listing.cend())
        m_retried.insert(path, {thumbnail.fileSize, thumbnail.modified});
    Q_EMIT changed(); // asked for again
}

void Gallery::keep(const QString &path, const Thumbnail &thumbnail)
{
    m_thumbnails.insert(path, thumbnail); // a null one stays: an unreadable file is not tried again
    // Kept: about three rows either side of what is shown.
    const int shown = capacity(m_scene.window.width());
    if (m_thumbnails.size() > 8 * shown + 64) {
        QSet<QString> keep;
        for (int i = -3 * shown; i < 4 * shown; ++i)
            if (const int file = fileAt(i); file >= 0)
                keep.insert(m_files.at(file));
        m_thumbnails.removeIf([&keep](const auto &entry) { return !keep.contains(entry.key()); });
    }
    Q_EMIT changed();
}

void Gallery::forgetChanged(const QHash<QString, FileStamp> &stamps)
{
    // A new listing: the thumbnails of files not in it (another folder, a file gone) go, and a
    // file written again since its thumbnail was made gets a new one. The listing's stamps, not
    // the disk: this runs on the interface's thread.
    m_thumbnails.removeIf([&stamps](const auto &entry) {
        const auto stamp = stamps.constFind(entry.key());
        return stamp == stamps.cend() || stamp->size != entry.value().fileSize || stamp->modified != entry.value().modified;
    });
    // One being decoded now is checked against these stamps when it arrives (arrived()).
    m_listing = stamps;
    m_retried.removeIf([&stamps](const auto &entry) { return !stamps.contains(entry.key()); });
}

void Gallery::rename(const QString &path, const QString &target)
{
    if (m_thumbnails.contains(path)) // the same content under its new name: not decoded again
        m_thumbnails.insert(target, m_thumbnails.take(path));
}

int Gallery::slotAt(const QPointF &position) const
{
    if (!contains(position))
        return -1;
    const QPointF inTexture = position - m_rect.topLeft() / m_scene.devicePixelRatio;
    for (int i = 0; i < m_slots.size(); ++i)
        if (m_slots.at(i).contains(inTexture))
            return i;
    return -1;
}

bool Gallery::contains(const QPointF &position) const
{
    // The background drawn, not the texture: the room left for the zoom is the image's.
    return !m_size.isEmpty() && m_shape.contains(position - m_rect.topLeft() / m_scene.devicePixelRatio);
}

void Gallery::setHover(int slot)
{
    if (slot == m_hover)
        return;
    m_hover = slot;
    Q_EMIT changed();
}

void Gallery::scroll(int steps)
{
    m_first += steps;
    Q_EMIT changed();
}

int Gallery::wheel(int angleDelta)
{
    m_wheel += angleDelta;
    const int steps = m_wheel / 120;
    m_wheel -= steps * 120;
    return steps;
}

void Gallery::recentre()
{
    m_centredOn = -2;
}
