#include "image.h"

#include "decoders.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

#if defined(Q_OS_WIN)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcDecode, "imageviewer.decode", QtWarningMsg)

namespace {

using color::Descriptor;
using color::Transfer;

// Runs fn(range) for consecutive ranges of [0, count) on the global thread pool.
struct Range {
    std::size_t begin;
    std::size_t end;
    float max;
    float maxLuminance;
};

template <typename Fn>
void forEachRange(std::size_t count, std::size_t chunk, Fn fn, std::vector<Range> *rangesOut = nullptr)
{
    std::vector<Range> ranges;
    for (std::size_t b = 0; b < count; b += chunk)
        ranges.push_back({b, std::min(count, b + chunk), 0.0f, 0.0f});
    QtConcurrent::blockingMap(ranges, fn);
    if (rangesOut)
        rangesOut->swap(ranges);
}

template <typename T>
float readSample(const unsigned char *p)
{
    T v;
    std::memcpy(&v, p, sizeof v);
    if constexpr (std::is_same_v<T, quint8>)
        return v / 255.0f;
    else if constexpr (std::is_same_v<T, quint16>)
        return v / 65535.0f;
    else
        return float(v); // qfloat16 or float
}

// Expands native samples of pixels [begin, end) into straight-alpha RGBA floats.
template <typename T>
void expandAs(const Decoded &dec, std::size_t begin, std::size_t end, float *dst)
{
    const std::size_t stride = std::size_t(dec.channels) * sizeof(T);
    const unsigned char *src = dec.data.get() + begin * stride;
    for (std::size_t i = begin; i < end; ++i, src += stride, dst += 4) {
        if (dec.gray) {
            dst[0] = dst[1] = dst[2] = readSample<T>(src);
        } else {
            dst[0] = readSample<T>(src);
            dst[1] = readSample<T>(src + sizeof(T));
            dst[2] = readSample<T>(src + 2 * sizeof(T));
        }
        const float a = dec.alphaIndex >= 0 ? readSample<T>(src + std::size_t(dec.alphaIndex) * sizeof(T)) : 1.0f;
        dst[3] = a;
        if (dec.associatedAlpha && a > 0.0f) // back to straight colour before any transfer function
            for (int c = 0; c < 3; ++c)
                dst[c] /= a;
    }
}

void expand(const Decoded &dec, std::size_t begin, std::size_t end, float *dst)
{
    switch (dec.sample) {
    case Decoded::Sample::U8: expandAs<quint8>(dec, begin, end, dst); break;
    case Decoded::Sample::U16: expandAs<quint16>(dec, begin, end, dst); break;
    case Decoded::Sample::F16: expandAs<qfloat16>(dec, begin, end, dst); break;
    case Decoded::Sample::F32: expandAs<float>(dec, begin, end, dst); break;
    }
}

// Rotates/mirrors a half-float RGBA buffer according to an EXIF orientation (2..8).
// Works in 64x64 tiles so that the transposing orientations stay cache friendly.
std::vector<qfloat16> orient(const std::vector<qfloat16> &src, int &w, int &h, int o)
{
    const int sw = w, sh = h;
    const bool swap = o >= 5;
    const int ow = swap ? sh : sw, oh = swap ? sw : sh;
    constexpr int kTile = 64;
    std::vector<qfloat16> out(src.size());
    forEachRange(std::size_t((oh + kTile - 1) / kTile), 1, [&](const Range &tileRows) {
        const int y0 = int(tileRows.begin) * kTile, y1 = std::min(oh, y0 + kTile);
        for (int x0 = 0; x0 < ow; x0 += kTile) {
            const int x1 = std::min(ow, x0 + kTile);
            for (int oy = y0; oy < y1; ++oy) {
                for (int ox = x0; ox < x1; ++ox) {
                    int sx = ox, sy = oy;
                    switch (o) {
                    case 2: sx = sw - 1 - ox; break;
                    case 3: sx = sw - 1 - ox; sy = sh - 1 - oy; break;
                    case 4: sy = sh - 1 - oy; break;
                    case 5: sx = oy; sy = ox; break;
                    case 6: sx = oy; sy = sh - 1 - ox; break;
                    case 7: sx = sw - 1 - oy; sy = sh - 1 - ox; break;
                    case 8: sx = sw - 1 - oy; sy = ox; break;
                    }
                    std::memcpy(&out[(std::size_t(oy) * ow + ox) * 4], &src[(std::size_t(sy) * sw + sx) * 4],
                                4 * sizeof(qfloat16));
                }
            }
        }
    });
    w = ow;
    h = oh;
    return out;
}

// Box-filters a premultiplied linear half-float buffer by an integer factor.
std::vector<qfloat16> downscale(const std::vector<qfloat16> &src, int &w, int &h, int factor)
{
    const int sw = w, sh = h;
    const int ow = (sw + factor - 1) / factor, oh = (sh + factor - 1) / factor;
    std::vector<qfloat16> out(std::size_t(ow) * oh * 4);
    forEachRange(std::size_t(oh), 16, [&](const Range &rows) {
        for (int oy = int(rows.begin); oy < int(rows.end); ++oy) {
            for (int ox = 0; ox < ow; ++ox) {
                double acc[4] = {0, 0, 0, 0};
                int n = 0;
                for (int y = oy * factor; y < std::min(sh, (oy + 1) * factor); ++y)
                    for (int x = ox * factor; x < std::min(sw, (ox + 1) * factor); ++x, ++n)
                        for (int c = 0; c < 4; ++c)
                            acc[c] += float(src[(std::size_t(y) * sw + x) * 4 + c]);
                for (int c = 0; c < 4; ++c)
                    out[(std::size_t(oy) * ow + ox) * 4 + c] = qfloat16(float(acc[c] / n));
            }
        }
    });
    w = ow;
    h = oh;
    return out;
}

// The display buffer of one decoded frame.
struct Converted {
    std::vector<qfloat16> pixels; // linear scRGB, premultiplied
    int width = 0;
    int height = 0;
    float maxComponent = 0.0f;
    float maxLuminance = 0.0f;
};

// One fused, parallel pass: native samples -> linear scRGB -> premultiplied half floats; then
// the orientation, applied once, and the reduction by `factor` (1: none).
Converted convertFrame(Decoded &dec, const color::Converter &converter, int factor)
{
    Converted out;
    const std::size_t pixelCount = std::size_t(dec.width) * dec.height;
    std::vector<qfloat16> pixels(pixelCount * 4);
    std::vector<Range> ranges;
    forEachRange(pixelCount, std::size_t(1) << 15, [&](Range &range) {
        const std::size_t n = range.end - range.begin;
        std::vector<float> rgba(n * 4);
        expand(dec, range.begin, range.end, rgba.data());
        converter.apply(rgba.data(), n);
        constexpr float kHalfMax = 65504.0f;
        float maxComponent = 0.0f, maxLuminance = 0.0f;
        for (std::size_t i = 0; i < n * 4; i += 4) {
            float *p = rgba.data() + i;
            const float a = std::isfinite(p[3]) ? std::clamp(p[3], 0.0f, 1.0f) : 1.0f;
            p[3] = a;
            for (int c = 0; c < 3; ++c)
                p[c] = std::isfinite(p[c]) ? std::clamp(p[c], -kHalfMax, kHalfMax) : 0.0f;
            if (a > 0.0f) { // invisible pixels do not drive tone mapping
                maxComponent = std::max({maxComponent, p[0], p[1], p[2]});
                maxLuminance = std::max(maxLuminance, color::luminance(p[0], p[1], p[2]));
            }
            for (int c = 0; c < 3; ++c)
                p[c] *= a;
        }
        qFloatToFloat16(pixels.data() + range.begin * 4, rgba.data(), qsizetype(n * 4));
        range.max = maxComponent;
        range.maxLuminance = maxLuminance;
    }, &ranges);
    for (const Range &r : ranges) {
        out.maxComponent = std::max(out.maxComponent, r.max);
        out.maxLuminance = std::max(out.maxLuminance, r.maxLuminance);
    }
    dec.data.reset(); // release native samples early

    int w = dec.width, h = dec.height;
    if (dec.orientation >= 2 && dec.orientation <= 8)
        pixels = orient(pixels, w, h, dec.orientation);
    if (factor > 1)
        pixels = downscale(pixels, w, h, factor);
    out.pixels = std::move(pixels);
    out.width = w;
    out.height = h;
    return out;
}

Image decode(const QString &path, int maxTextureSize, qint64 maxPixels)
{
    QElapsedTimer timer;
    timer.start();
    Image result;
    result.path = path;
    // Before reading: a change during the decode then shows as a different file next time.
    const QFileInfo info(path);
    result.fileSize = info.size();
    result.modified = info.lastModified();

    Decoded dec;
    QString error;
    std::unique_ptr<FrameReader> frames;
    if (!decodeFile(path, maxPixels, &dec, &error, &frames)) {
        if (dec.overLimit) {
            result.overPixelLimit = true;
            result.error = QStringLiteral("larger than the pixel limit"); // never shown
            return result;
        }
        result.error = QCoreApplication::translate("Image", "Cannot decode: %1").arg(error);
        return result;
    }
    const qint64 readNs = timer.nsecsElapsed();

    // Integer samples normalised to [0, 1] are exact multiples of 1/(2^n - 1).
    const int integerBits = dec.sample == Decoded::Sample::U8 ? 8 : dec.sample == Decoded::Sample::U16 ? 16 : 0;
    auto converter = std::make_unique<color::Converter>(dec.colour, integerBits);
    if (!converter->error().isEmpty()) {
        // An unusable embedded profile must not block viewing; fall back visibly (F12).
        const QString reason = converter->error();
        dec.colour = Descriptor();
        dec.colour.transfer = dec.isInteger() ? Transfer::Srgb : Transfer::Linear;
        dec.colour.description = QCoreApplication::translate("Image", "%1 (assumed: %2)")
                                     .arg(dec.isInteger() ? QStringLiteral("sRGB") : QStringLiteral("linear BT.709"),
                                          reason);
        converter = std::make_unique<color::Converter>(dec.colour, integerBits);
    }
    const bool littleCms = converter->usesLittleCms();

    // Orientation swaps the axes but keeps the longest side, so the reduction is known here.
    const int longest = std::max(dec.width, dec.height);
    const int factor = maxTextureSize > 0 && longest > maxTextureSize ? (longest + maxTextureSize - 1) / maxTextureSize : 1;
    const bool swapsAxes = dec.orientation >= 5 && dec.orientation <= 8;
    result.sourceWidth = swapsAxes ? dec.height : dec.width;
    result.sourceHeight = swapsAxes ? dec.width : dec.height;
    result.codec = dec.codec;
    result.sourceChannels = dec.sourceChannels;
    result.sourceBits = dec.bits;
    result.sourceFloat = !dec.isInteger();
    result.hasAlpha = dec.alphaIndex >= 0;
    result.orientation = dec.orientation;
    result.colour = dec.colour;
    result.camera = dec.camera;
    const int firstDurationMs = dec.durationMs;
    Converted first = convertFrame(dec, *converter, factor);
    const qint64 convertNs = timer.nsecsElapsed();

    result.width = first.width;
    result.height = first.height;
    result.maxComponent = first.maxComponent;
    result.maxLuminance = first.maxLuminance;
    result.pixels = std::make_shared<const std::vector<qfloat16>>(std::move(first.pixels));
    if (frames)
        result.animation = std::make_shared<Animation>(std::move(frames), std::move(converter), factor, result.width,
                                                       result.height, result.pixels, firstDurationMs);
    result.decodeMs = double(timer.nsecsElapsed()) / 1e6;
    qCInfo(lcDecode).nospace() << QFileInfo(path).fileName() << ": read " << readNs / 1000000 << " ms, convert "
                               << (convertNs - readNs) / 1000000 << " ms" << (littleCms ? " (LittleCMS)" : "")
                               << (result.animation ? ", animated" : "");
    return result;
}

} // namespace

Animation::Animation(std::unique_ptr<FrameReader> reader, std::unique_ptr<color::Converter> converter, int factor,
                     int width, int height, PixelBuffer first, int firstDurationMs)
    : m_reader(std::move(reader)), m_converter(std::move(converter)), m_factor(factor), m_width(width),
      m_height(height), m_loops(m_reader->loopCount()),
      // Every frame stays in memory while they fit in an eighth of the RAM (at least 128 MiB,
      // at most 1 GiB); beyond that, each loop decodes them again.
      m_budget(std::clamp(physicalMemoryBytes() / 8, qint64(128) << 20, qint64(1) << 30))
{
    m_count = m_reader->frameCount();
    m_kept.push_back({0, std::move(first), firstDurationMs});
    m_keptBytes = qint64(m_kept.front().pixels->size() * sizeof(qfloat16));
}

Animation::~Animation() = default;

bool Animation::frame(int index, Frame *out, QString *error)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    const int known = m_count.load();
    if (index < 0 || (known > 0 && index >= known))
        index = 0;
    if (index < int(m_kept.size()) && m_kept[index].pixels) {
        *out = m_kept[index];
        return true;
    }
    if (index < m_next) { // passed already and not kept: from the start again
        if (!m_reader->rewind(error))
            return false;
        m_next = 0;
    }
    for (;;) {
        Decoded dec;
        int durationMs = 0;
        QString why;
        if (!m_reader->next(&dec, &durationMs, &why)) {
            if (!why.isEmpty()) {
                *error = why;
                return false;
            }
            // Past the last frame: the count is known now, and playback goes on from the first.
            m_count = std::max(1, m_next);
            m_reader->rewind(&why);
            m_next = 0;
            *out = m_kept.front();
            return true;
        }
        const int at = m_next++;
        if (m_reader->frameCount() > 0)
            m_count = m_reader->frameCount();
        const bool kept = at < int(m_kept.size()) && m_kept[at].pixels;
        if (kept || (at < index && !m_keepAll)) {
            if (at == index) {
                *out = m_kept[at];
                return true;
            }
            continue; // read past without converting
        }
        Converted c = convertFrame(dec, *m_converter, m_factor);
        if (c.width != m_width || c.height != m_height) {
            *error = QCoreApplication::translate("Image", "invalid dimensions (%1×%2×%3)").arg(c.width).arg(c.height).arg(4);
            return false;
        }
        Frame frame{at, std::make_shared<const std::vector<qfloat16>>(std::move(c.pixels)), durationMs};
        const qint64 bytes = qint64(frame.pixels->size() * sizeof(qfloat16));
        if (m_keepAll && m_keptBytes + bytes <= m_budget) {
            if (int(m_kept.size()) <= at)
                m_kept.resize(std::size_t(at) + 1);
            m_kept[std::size_t(at)] = frame;
            m_keptBytes += bytes;
        } else if (m_keepAll) { // too many to keep: only the first stays, for showing it at once
            m_keepAll = false;
            m_kept.resize(1);
            m_keptBytes = qint64(m_kept.front().pixels->size() * sizeof(qfloat16));
        }
        if (at == index) {
            *out = std::move(frame);
            return true;
        }
    }
}

Image decodeImage(const QString &path, int maxTextureSize, qint64 maxPixels)
{
    // Runs on a worker thread: an exception escaping here would be rethrown by
    // QFuture::result() on the GUI thread and terminate the application.
    try {
        return decode(path, maxTextureSize, maxPixels);
    } catch (const std::bad_alloc &) {
        Image failed;
        failed.path = path;
        failed.error = QCoreApplication::translate("Image", "Not enough memory to decode the image.");
        return failed;
    } catch (const std::exception &e) {
        Image failed;
        failed.path = path;
        failed.error = QCoreApplication::translate("Image", "Decoding error: %1").arg(QString::fromLocal8Bit(e.what()));
        return failed;
    }
}

QImage decodeForClipboard(const QString &path)
{
    Image image = decodeImage(path, 0);
    if (!image.isValid())
        return {};
    QImage out(image.width, image.height, QImage::Format_RGBA64);
    if (out.isNull())
        return {};
    const std::size_t width = std::size_t(image.width);
    forEachRange(std::size_t(image.height), 64, [&](Range &rows) {
        for (std::size_t y = rows.begin; y < rows.end; ++y) {
            const qfloat16 *src = image.pixels->data() + y * width * 4;
            auto *dst = reinterpret_cast<QRgba64 *>(out.scanLine(int(y)));
            for (std::size_t x = 0; x < width; ++x, src += 4) {
                const float alpha = std::clamp(float(src[3]), 0.0f, 1.0f);
                quint16 v[3];
                for (int c = 0; c < 3; ++c) {
                    const float straight = alpha > 0.0f ? float(src[c]) / alpha : 0.0f;
                    v[c] = quint16(std::lround(color::linearToSrgb(std::clamp(straight, 0.0f, 1.0f)) * 65535.0f));
                }
                dst[x] = QRgba64::fromRgba64(v[0], v[1], v[2], quint16(std::lround(alpha * 65535.0f)));
            }
        }
    });
    out.setColorSpace(QColorSpace::SRgb);
    return out;
}

void shutdownDecoders()
{
    shutdownOpenImageIO();
}

QString exposureTimeText(float seconds, const QLocale &locale)
{
    // Fractions only where they are whole (1/3, 1/8000); 0.4 s stays a decimal, not "1/3".
    const double reciprocal = seconds > 0.0f ? 1.0 / double(seconds) : 0.0;
    if (seconds >= 1.0f || reciprocal < 1.5 || std::abs(reciprocal - std::round(reciprocal)) > 0.02 * reciprocal)
        return locale.toString(double(seconds), 'g', 3);
    return QStringLiteral("1/") + locale.toString(qlonglong(std::llround(reciprocal)));
}

qint64 physicalMemoryBytes()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? qint64(status.ullTotalPhys) : 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && pageSize > 0 ? qint64(pages) * pageSize : 0;
#endif
}
