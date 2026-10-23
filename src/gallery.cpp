// ViewerWindow, part 7: the gallery (D-60). The folder's images as thumbnails in a bar at the
// bottom: the one under the pointer enlarges (the zoom), a click shows that image, the wheel
// scrolls. Thumbnails are decoded on their own threads,
// colour-managed to sRGB like the clipboard, and drawn into one texture the renderer composites
// at SDR white, like the information panels.
#include "viewer.h"

#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kBorder = 8.0;       // between thumbnails and around them
constexpr double kSideFree = 128.0;   // left free on each side of the window
constexpr double kTileMargin = 3.0;   // the tiles reach this far around a thumbnail
constexpr int kHoverFade = 10;        // the hovered tile: the highlight 10 points less opaque (D-65)
constexpr int kPrefetch = 1;          // thumbnails decoded ahead: this many rows' worth either side

// sRGB, 8 bits, premultiplied in encoded values like every overlay texture; HDR values above SDR
// white and colours outside sRGB are clipped, as on the clipboard (a thumbnail is a preview).
QImage thumbnailFrom(const Image &image)
{
    QImage out(image.width, image.height, QImage::Format_RGBA8888_Premultiplied);
    if (out.isNull())
        return {};
    const qfloat16 *src = image.pixels->data();
    for (int y = 0; y < image.height; ++y) {
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < image.width; ++x, src += 4, dst += 4) {
            const float alpha = std::clamp(float(src[3]), 0.0f, 1.0f);
            for (int c = 0; c < 3; ++c) {
                const float straight = alpha > 0.0f ? float(src[c]) / alpha : 0.0f;
                dst[c] = uchar(std::lround(color::linearToSrgb(std::clamp(straight, 0.0f, 1.0f)) * alpha * 255.0f));
            }
            dst[3] = uchar(std::lround(alpha * 255.0f));
        }
    }
    return out;
}
} // namespace

double ViewerWindow::galleryThumbnail() const
{
    return m_settings.galleryThumbnailSize; // logical pixels, square
}

int ViewerWindow::galleryZoom() const
{
    return m_settings.galleryZoom ? m_settings.galleryEnlargement : 0;
}

double ViewerWindow::galleryOverhang() const
{
    // The thumbnail under the pointer grows by the zoom about the bar's middle line; its tile, and
    // the bar's background behind it, reach this far above and below the bar.
    return std::max(0.0, galleryZoom() / 2.0 + kTileMargin - kBorder);
}

int ViewerWindow::galleryCapacity() const
{
    // n thumbnails of side t take n·t + (n + 1)·8 px, the zoom widens the row by its own size, and 128 px
    // stay free on either side.
    const double room = width() - 2 * kSideFree - kBorder - galleryZoom();
    return std::max(1, int(std::floor(room / (galleryThumbnail() + kBorder))));
}

double ViewerWindow::galleryBottomMargin() const
{
    // From the window's bottom to the bar: kEdgeMargin, or more when the enlarged thumbnail's tile
    // reaches further below the bar; it then touches the bottom (D-61).
    return std::max(kEdgeMargin, galleryOverhang());
}

double ViewerWindow::galleryReserve() const
{
    // The bar with the tile's reach above and below it, and its margin: the information panel
    // stays above all of it.
    return m_gallerySize.isEmpty() ? 0.0 : galleryBottomMargin() + galleryThumbnail() + 2 * kBorder + galleryOverhang();
}

bool ViewerWindow::galleryShown() const
{
    return m_settings.showGallery && !m_files.isEmpty();
}

int ViewerWindow::galleryFile(int slot) const
{
    // Navigation's loop makes the gallery a ring: after the last image, the first again.
    const int count = int(m_files.size());
    const int file = m_galleryFirst + slot;
    if (m_settings.loop && count > 0)
        return (file % count + count) % count;
    return file >= 0 && file < count ? file : -1;
}

void ViewerWindow::updateGallery()
{
    if (!m_rendererReady)
        return;
    if (!galleryShown()) {
        m_thumbnailQueue.clear();
        if (!m_gallerySize.isEmpty()) {
            m_gallerySize = QSize();
            m_galleryKey.clear(); // drawn again when it shows again
            m_gallerySlots.clear();
            m_galleryHover = -1;
            m_renderer.setOverlay(Renderer::GalleryLayer, QImage());
            requestUpdate();
        }
        return;
    }
    const int count = int(m_files.size());
    // When not every image fits, an odd number: the current thumbnail then sits in the middle.
    const int capacity = galleryCapacity();
    const int shown = count <= capacity ? count : std::max(1, capacity % 2 ? capacity : capacity - 1);
    // The current image comes to the centre whenever it changes; the wheel moves away from it.
    // When the window holds more or fewer thumbnails, the middle one stays (both counts are odd).
    if (!m_gallerySlots.isEmpty())
        m_galleryFirst += (int(m_gallerySlots.size()) - shown) / 2;
    if (m_index != m_galleryCentredOn) {
        m_galleryCentredOn = m_index;
        m_galleryFirst = std::max(0, m_index) - shown / 2;
    }
    m_galleryFirst = m_settings.loop && count > shown ? galleryFile(0) : std::clamp(m_galleryFirst, 0, count - shown);
    if (m_galleryHover >= shown)
        m_galleryHover = -1;

    // Thumbnails for what is shown first, then a row's worth either side. Asked again at every
    // change: what scrolled out of reach before its turn is never decoded (the pool's own queue
    // would decode it before what is shown now).
    QStringList wanted;
    for (int i = 0; i < shown; ++i)
        wanted << m_files.at(galleryFile(i));
    for (int i = -kPrefetch * shown; i < (kPrefetch + 1) * shown; ++i)
        if (const int file = galleryFile(i); file >= 0)
            wanted << m_files.at(file);
    m_thumbnailQueue.clear();
    for (const QString &path : std::as_const(wanted))
        if (!m_thumbnails.contains(path) && !m_thumbnailsPending.contains(path) && !m_thumbnailQueue.contains(path))
            m_thumbnailQueue << path;
    startThumbnails();

    const double dpr = devicePixelRatio();
    const int zoom = galleryZoom();
    const double overhang = galleryOverhang();
    const double barHeight = galleryThumbnail() + 2 * kBorder;
    const QSizeF logical(shown * (galleryThumbnail() + kBorder) + kBorder + zoom, barHeight + 2 * overhang);

    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|")
                      .arg(m_galleryFirst).arg(shown).arg(m_galleryHover).arg(m_index).arg(dpr)
                      .arg(m_settings.galleryOpacity).arg(zoom).arg(galleryThumbnail())
                  + m_settings.galleryHighlight.name(QColor::HexRgb);
    for (int i = 0; i < shown; ++i) { // which file, and whether its thumbnail is there yet
        const QString &path = m_files.at(galleryFile(i));
        key += QLatin1Char(m_thumbnails.contains(path) ? '+' : '-') + QString::number(qHash(path), 36);
    }
    m_galleryRect = QRectF(QPointF(std::round((width() - logical.width()) / 2.0 * dpr),
                                   std::round((height() - galleryBottomMargin() - barHeight - overhang) * dpr)),
                           logical * dpr);
    if (key == m_galleryKey)
        return;
    m_galleryKey = key;

    // The places: the thumbnail under the pointer is larger by the zoom, the others move aside.
    const double rowWidth = shown * (galleryThumbnail() + kBorder) + kBorder + (m_galleryHover >= 0 ? zoom : 0);
    const double middle = overhang + barHeight / 2.0; // thumbnails grow evenly about it
    QList<QRectF> places;
    double x = (logical.width() - rowWidth) / 2.0 + kBorder;
    for (int i = 0; i < shown; ++i) {
        const double size = galleryThumbnail() + (i == m_galleryHover ? zoom : 0);
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
    if (m_galleryHover >= 0) {
        QPainterPath tile;
        tile.addRoundedRect(tileOf(places.at(m_galleryHover)), kPanelRadius / 2, kPanelRadius / 2);
        background = background.united(tile);
    }
    const int alpha = int(std::lround(m_settings.galleryOpacity * 2.55));
    painter.setPen(Qt::NoPen);
    painter.fillPath(background, QColor(0, 0, 0, alpha));
    m_galleryShape = background;

    // The pointer is matched against the places without the zoom: matched against the enlarged
    // one, a thumbnail growing under the pointer pushed it onto its neighbour, which grew in turn,
    // and the hover flickered between them. Each place takes half the gap either side.
    m_gallerySlots.clear();
    for (int i = 0; i < shown; ++i)
        m_gallerySlots << QRectF((zoom + kBorder) / 2.0 + i * (galleryThumbnail() + kBorder), 0, galleryThumbnail() + kBorder, logical.height());

    for (int i = 0; i < shown; ++i) {
        const QRectF &place = places.at(i);
        const QString &path = m_files.at(galleryFile(i));
        // One colour behind the current thumbnail and the hovered one, at the gallery's opacity
        // over its background; the hovered one a little fainter, unless it is the current one.
        if (galleryFile(i) == m_index || i == m_galleryHover) {
            QColor tile = m_settings.galleryHighlight;
            const int opacity = galleryFile(i) == m_index ? m_settings.galleryOpacity
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
    m_gallerySize = image.size();
    m_renderer.setOverlay(Renderer::GalleryLayer, image);
    requestUpdate();
}

void ViewerWindow::startThumbnails()
{
    while (!m_thumbnailQueue.isEmpty() && m_thumbnailsPending.size() < m_thumbnailPool.maxThreadCount())
        decodeThumbnail(m_thumbnailQueue.takeFirst());
}

void ViewerWindow::decodeThumbnail(const QString &path)
{
    m_thumbnailsPending.insert(path);
    // Decoded at the largest size the zoom can show with the largest thumbnails, reduced in linear
    // light (decodeImage): another thumbnail size then decodes nothing again.
    const int pixels = int(std::ceil((Settings::kGalleryThumbnailSizes.back() + Settings::kMaxGalleryEnlargement)
                                     * devicePixelRatio()));
    // Within the preload's pixel limit: a huge image is decoded whole before it is reduced.
    const qint64 maxPixels = m_cache.maxPreloadPixels();
    m_thumbnailPool.start([this, path, pixels, maxPixels, generation = m_thumbnailGeneration] {
        Thumbnail thumbnail;
        const QFileInfo info(path); // before decoding: a file changed meanwhile is decoded again
        thumbnail.fileSize = info.size();
        thumbnail.modified = info.lastModified();
        if (const Image image = decodeImage(path, pixels, maxPixels, false); image.isValid())
            thumbnail.image = thumbnailFrom(image);
        QMetaObject::invokeMethod(this, [this, path, thumbnail, generation] {
            m_thumbnailsPending.remove(path);
            if (generation == m_thumbnailGeneration) // otherwise decoded before the folder changed: asked again
                thumbnailReady(path, thumbnail);
            else
                updateGallery();
        }, Qt::QueuedConnection);
    });
}

void ViewerWindow::thumbnailReady(const QString &path, const Thumbnail &thumbnail)
{
    m_thumbnails.insert(path, thumbnail); // a null one stays: an unreadable file is not tried again
    // Kept: about three rows either side of what is shown.
    const int shown = galleryCapacity();
    if (m_thumbnails.size() > 8 * shown + 64) {
        QSet<QString> keep;
        for (int i = -3 * shown; i < 4 * shown; ++i)
            if (const int file = galleryFile(i); file >= 0)
                keep.insert(m_files.at(file));
        m_thumbnails.removeIf([&keep](const auto &entry) { return !keep.contains(entry.key()); });
    }
    updateGallery();
}

void ViewerWindow::forgetChangedThumbnails()
{
    // The folder changed: a file written again since its thumbnail was made gets a new one.
    m_thumbnails.removeIf([](const auto &entry) {
        const QFileInfo info(entry.key());
        return info.size() != entry.value().fileSize || info.lastModified() != entry.value().modified;
    });
    ++m_thumbnailGeneration; // and one being decoded now may have read the file before it changed
}

int ViewerWindow::galleryIndexAt(const QPointF &position) const
{
    if (!galleryContains(position))
        return -1;
    const QPointF inTexture = position - m_galleryRect.topLeft() / devicePixelRatio();
    for (int i = 0; i < m_gallerySlots.size(); ++i)
        if (m_gallerySlots.at(i).contains(inTexture))
            return i;
    return -1;
}

bool ViewerWindow::galleryContains(const QPointF &position) const
{
    // The background drawn, not the texture: the room left for the zoom is the image's.
    return !m_gallerySize.isEmpty() && m_galleryShape.contains(position - m_galleryRect.topLeft() / devicePixelRatio());
}

void ViewerWindow::setGalleryHover(int slot)
{
    if (slot == m_galleryHover)
        return;
    m_galleryHover = slot;
    updateGallery();
}

void ViewerWindow::galleryClicked(int slot)
{
    const int index = galleryFile(slot);
    if (index < 0 || index == m_index)
        return;
    m_direction = index > m_index ? 1 : -1;
    restartSlideshowInterval();
    startLoading(index);
}

void ViewerWindow::scrollGallery(int steps)
{
    m_galleryFirst += steps;
    updateGallery();
}

void ViewerWindow::toggleGallery()
{
    savePreference([on = !m_settings.showGallery](Settings &s) { s.showGallery = on; });
    m_galleryCentredOn = -2; // centre on the current image when it shows again
    updateOverlay();         // the information panel moves above the gallery, or back
}
