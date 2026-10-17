// ViewerWindow, part 7: the gallery (D-60). The folder's images as thumbnails in a bar at the
// bottom, like a dock: the one under the pointer and its neighbours enlarge (the carousel), a
// click shows that image, the wheel scrolls. Thumbnails are decoded on their own threads,
// colour-managed to sRGB like the clipboard, and drawn into one texture the renderer composites
// at SDR white, like the information panels.
#include "viewer.h"

#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kThumbnail = 128.0;  // logical pixels, square
constexpr double kBorder = 8.0;       // between thumbnails and around them
constexpr double kSideFree = 128.0;   // left free on each side of the window
// Behind the current thumbnail, and lighter (halfway to white) behind the one under the pointer;
// at the gallery's background opacity, over that background.
const QColor kSelected(255, 140, 0);
const QColor kHovered(255, 198, 128);
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

int ViewerWindow::galleryEnlargement(int offset) const
{
    // The carousel: the thumbnail under the pointer grows by the setting, its neighbours by a
    // half and a quarter of it.
    if (!m_settings.galleryCarousel || m_galleryHover < 0)
        return 0;
    const int distance = std::abs(offset - m_galleryHover);
    return distance == 0 ? m_settings.galleryEnlargement
           : distance == 1 ? m_settings.galleryEnlargement / 2
           : distance == 2 ? m_settings.galleryEnlargement / 4
                           : 0;
}

int ViewerWindow::galleryCapacity() const
{
    // n thumbnails take n·128 + (n + 1)·8 px; the carousel adds at most 2.5 times the setting
    // (the hovered one and two neighbours on each side), and 256 px stay free on either side.
    const int g = m_settings.galleryCarousel ? m_settings.galleryEnlargement : 0;
    const double carousel = g + 2 * (g / 2) + 2 * (g / 4);
    const double room = width() - 2 * kSideFree - kBorder - carousel;
    return std::max(1, int(std::floor(room / (kThumbnail + kBorder))));
}

double ViewerWindow::galleryReserve() const
{
    // The gallery's bar and the margin above it: the information panel stays above both.
    return m_gallerySize.isEmpty() ? 0.0 : kThumbnail + 2 * kBorder + galleryBottomMargin();
}

double ViewerWindow::galleryBottomMargin() const
{
    // The bar keeps kEdgeMargin from the bottom; the carousel enlarges a thumbnail about the bar's
    // middle line, so beyond 48 px of enlargement the bar rises to keep it on screen.
    const int g = m_settings.galleryCarousel ? m_settings.galleryEnlargement : 0;
    return std::max(kEdgeMargin, g / 2.0 - kBorder);
}

bool ViewerWindow::galleryShown() const
{
    return m_settings.showGallery && !m_files.isEmpty();
}

void ViewerWindow::updateGallery()
{
    if (!m_rendererReady)
        return;
    if (!galleryShown()) {
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
    if (m_index != m_galleryCentredOn) {
        m_galleryCentredOn = m_index;
        m_galleryFirst = std::max(0, m_index) - shown / 2;
    }
    m_galleryFirst = std::clamp(m_galleryFirst, 0, count - shown);
    if (m_galleryHover >= shown)
        m_galleryHover = -1;

    // Thumbnails for what is shown first, then a row's worth either side.
    const int from = std::max(0, m_galleryFirst - kPrefetch * shown);
    const int to = std::min(count, m_galleryFirst + shown + kPrefetch * shown);
    for (int i = m_galleryFirst; i < m_galleryFirst + shown; ++i)
        requestThumbnail(m_files.at(i));
    for (int i = from; i < to; ++i)
        requestThumbnail(m_files.at(i));

    const double dpr = devicePixelRatio();
    const int g = m_settings.galleryCarousel ? m_settings.galleryEnlargement : 0;
    QList<double> sizes;
    double rowWidth = kBorder;
    for (int i = 0; i < shown; ++i) {
        sizes << kThumbnail + galleryEnlargement(i);
        rowWidth += sizes.last() + kBorder;
    }
    const double carousel = g + 2 * (g / 2) + 2 * (g / 4);
    const QSizeF logical(shown * (kThumbnail + kBorder) + kBorder + carousel, kThumbnail + 2 * kBorder + g);

    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                      .arg(m_galleryFirst).arg(shown).arg(m_galleryHover).arg(m_index).arg(dpr)
                      .arg(m_settings.galleryOpacity).arg(g);
    for (int i = 0; i < shown; ++i)
        key += m_thumbnails.contains(m_files.at(m_galleryFirst + i)) ? QLatin1Char('1') : QLatin1Char('0');
    // The texture reaches g/2 above and below the bar, for the enlarged thumbnails.
    m_galleryRect = QRectF(QPointF(std::round((width() - logical.width()) / 2.0 * dpr),
                                   std::round((height() - galleryBottomMargin() - kThumbnail - 2 * kBorder - g / 2.0) * dpr)),
                           logical * dpr);
    if (key == m_galleryKey)
        return;
    m_galleryKey = key;

    QImage image(QSize(int(std::ceil(logical.width() * dpr)), int(std::ceil(logical.height() * dpr))),
                 QImage::Format_RGBA8888_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // The background keeps its height; enlarged thumbnails grow about its middle line.
    const QRectF background((logical.width() - rowWidth) / 2.0, g / 2.0, rowWidth, kThumbnail + 2 * kBorder);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, int(std::lround(m_settings.galleryOpacity * 2.55))));
    painter.drawRoundedRect(background, kPanelRadius, kPanelRadius);

    // The pointer is matched against the thumbnails' places without the carousel: matched against
    // the enlarged ones, a thumbnail growing under the pointer pushed it onto its neighbour, which
    // grew in turn, and the hover flickered between them. Each place takes half the gap either side.
    m_gallerySlots.clear();
    const double baseLeft = (logical.width() - (shown * (kThumbnail + kBorder) + kBorder)) / 2.0 + kBorder / 2.0;
    for (int i = 0; i < shown; ++i)
        m_gallerySlots << QRectF(baseLeft + i * (kThumbnail + kBorder), 0, kThumbnail + kBorder, logical.height());

    const int tileAlpha = int(std::lround(m_settings.galleryOpacity * 2.55));
    const double middle = background.center().y(); // thumbnails grow evenly about it
    double x = background.left() + kBorder;
    for (int i = 0; i < shown; ++i) {
        const QRectF slot(x, middle - sizes.at(i) / 2.0, sizes.at(i), sizes.at(i));
        x += sizes.at(i) + kBorder;
        if (m_galleryFirst + i == m_index || i == m_galleryHover) {
            QColor tile = m_galleryFirst + i == m_index ? kSelected : kHovered;
            tile.setAlpha(tileAlpha);
            painter.setBrush(tile);
            painter.drawRoundedRect(slot.adjusted(-3, -3, 3, 3), kPanelRadius / 2, kPanelRadius / 2);
        }
        const QImage thumbnail = m_thumbnails.value(m_files.at(m_galleryFirst + i));
        if (thumbnail.isNull()) {
            painter.setBrush(QColor(255, 255, 255, 28)); // still being decoded, or unreadable
            painter.drawRoundedRect(slot, kPanelRadius / 2, kPanelRadius / 2);
        } else {
            QSizeF fitted = QSizeF(thumbnail.size()).scaled(slot.size(), Qt::KeepAspectRatio);
            painter.drawImage(QRectF(slot.center() - QPointF(fitted.width(), fitted.height()) / 2.0, fitted), thumbnail);
        }
    }
    painter.end();
    m_gallerySize = image.size();
    m_renderer.setOverlay(Renderer::GalleryLayer, image);
    requestUpdate();
}

void ViewerWindow::requestThumbnail(const QString &path)
{
    if (m_thumbnails.contains(path) || m_thumbnailsPending.contains(path))
        return;
    m_thumbnailsPending.insert(path);
    // Decoded at the largest size the carousel can show, reduced in linear light (decodeImage).
    const int pixels = int(std::ceil((kThumbnail + Settings::kMaxGalleryEnlargement) * devicePixelRatio()));
    // Within the preload's pixel limit: a huge image is decoded whole before it is reduced.
    const qint64 maxPixels = m_cache.maxPreloadPixels();
    m_thumbnailPool.start([this, path, pixels, maxPixels] {
        const Image image = decodeImage(path, pixels, maxPixels);
        const QImage thumbnail = image.isValid() ? thumbnailFrom(image) : QImage();
        QMetaObject::invokeMethod(this, [this, path, thumbnail] { thumbnailReady(path, thumbnail); }, Qt::QueuedConnection);
    });
}

void ViewerWindow::thumbnailReady(const QString &path, const QImage &thumbnail)
{
    m_thumbnailsPending.remove(path);
    m_thumbnails.insert(path, thumbnail); // a null one stays: an unreadable file is not tried again
    // Kept: about three rows either side of what is shown.
    if (m_thumbnails.size() > 8 * std::max(1, galleryCapacity()) + 64) {
        const int shown = galleryCapacity();
        const int from = std::max(0, m_galleryFirst - 3 * shown);
        const int to = std::min(int(m_files.size()), m_galleryFirst + 4 * shown);
        const QSet<QString> keep(m_files.cbegin() + from, m_files.cbegin() + to);
        for (auto it = m_thumbnails.begin(); it != m_thumbnails.end();)
            it = keep.contains(it.key()) ? std::next(it) : m_thumbnails.erase(it);
    }
    updateGallery();
}

int ViewerWindow::galleryIndexAt(const QPointF &position) const
{
    const QPointF inTexture = position - m_galleryRect.topLeft() / devicePixelRatio();
    for (int i = 0; i < m_gallerySlots.size(); ++i)
        if (m_gallerySlots.at(i).contains(inTexture))
            return i;
    return -1;
}

bool ViewerWindow::galleryContains(const QPointF &position) const
{
    return !m_gallerySize.isEmpty() && QRectF(m_galleryRect.topLeft() / devicePixelRatio(),
                                              QSizeF(m_gallerySize) / devicePixelRatio()).contains(position);
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
    const int index = m_galleryFirst + slot;
    if (index < 0 || index >= m_files.size() || index == m_index)
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
