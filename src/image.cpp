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
    if (!decodeFile(path, maxPixels, &dec, &error)) {
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

    // One fused, parallel pass: native samples -> linear scRGB -> premultiplied half floats.
    const std::size_t pixelCount = std::size_t(dec.width) * dec.height;
    std::vector<qfloat16> pixels(pixelCount * 4);
    std::vector<Range> ranges;
    forEachRange(pixelCount, std::size_t(1) << 15, [&](Range &range) {
        const std::size_t n = range.end - range.begin;
        std::vector<float> rgba(n * 4);
        expand(dec, range.begin, range.end, rgba.data());
        converter->apply(rgba.data(), n);
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
    float maxComponent = 0.0f, maxLuminance = 0.0f;
    for (const Range &r : ranges) {
        maxComponent = std::max(maxComponent, r.max);
        maxLuminance = std::max(maxLuminance, r.maxLuminance);
    }
    dec.data.reset(); // release native samples early
    const qint64 convertNs = timer.nsecsElapsed();

    int w = dec.width, h = dec.height;
    if (dec.orientation >= 2 && dec.orientation <= 8)
        pixels = orient(pixels, w, h, dec.orientation);
    result.sourceWidth = w;
    result.sourceHeight = h;
    const int longest = std::max(w, h);
    if (maxTextureSize > 0 && longest > maxTextureSize)
        pixels = downscale(pixels, w, h, (longest + maxTextureSize - 1) / maxTextureSize);

    result.width = w;
    result.height = h;
    result.codec = dec.codec;
    result.sourceChannels = dec.sourceChannels;
    result.sourceBits = dec.bits;
    result.sourceFloat = !dec.isInteger();
    result.hasAlpha = dec.alphaIndex >= 0;
    result.orientation = dec.orientation;
    result.colour = dec.colour;
    result.camera = dec.camera;
    result.maxComponent = maxComponent;
    result.maxLuminance = maxLuminance;
    result.pixels = std::make_shared<const std::vector<qfloat16>>(std::move(pixels));
    result.decodeMs = double(timer.nsecsElapsed()) / 1e6;
    qCInfo(lcDecode).nospace() << QFileInfo(path).fileName() << ": read " << readNs / 1000000 << " ms, convert "
                               << (convertNs - readNs) / 1000000 << " ms, orient/downscale "
                               << (timer.nsecsElapsed() - convertNs) / 1000000 << " ms"
                               << (converter->usesLittleCms() ? " (LittleCMS)" : "");
    return result;
}

} // namespace

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
