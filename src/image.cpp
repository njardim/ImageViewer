#include "image.h"

#include <OpenImageIO/imageio.h>

#include <QColorSpace>
#include <QElapsedTimer>
#include <QHash>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLoggingCategory>
#include <QSet>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

Q_LOGGING_CATEGORY(lcDecode, "imageviewer.decode", QtWarningMsg)

namespace {

using color::Descriptor;
using color::Transfer;

// Decoder output before colour conversion: interleaved samples in their native type.
struct Decoded {
    enum class Sample { U8, U16, F32 };
    int width = 0;
    int height = 0;
    Sample sample = Sample::U8;
    int channels = 0;    // interleaved channels in `data`
    int alphaIndex = -1; // channel holding straight alpha, -1 if none
    bool gray = false;   // channel 0 is luminance
    std::vector<unsigned char> data;
    int bits = 0;        // significant bits per sample (informative)
    int orientation = 1;
    Descriptor colour;
    QString codec;

    int sampleBytes() const { return sample == Sample::U8 ? 1 : sample == Sample::U16 ? 2 : 4; }
};

constexpr qint64 kMaxPixels = qint64(1) << 30; // refuse absurd dimensions before allocating

// Parses an OpenImageIO colour space name. OIIO 3 uses colour interop ids of the
// form <transfer>_<primaries>_<scene|display>; OIIO 2 used ad-hoc names.
bool parseOiioColorSpace(const QString &name, Descriptor *d)
{
    const QString n = name.toLower();
    if (n.isEmpty())
        return false;
    const QStringList parts = n.split(QLatin1Char('_'));
    if (parts.size() >= 2) {
        static const QHash<QString, Transfer> transfers = {
            {"lin", Transfer::Linear}, {"srgb", Transfer::Srgb}, {"srgbe", Transfer::Srgb},
            {"g18", Transfer::Gamma18}, {"g22", Transfer::Gamma22}, {"g24", Transfer::Bt1886},
            {"pq", Transfer::Pq}, {"hlg", Transfer::Hlg}};
        static const QHash<QString, color::Chromaticities> primaries = {
            {"rec709", color::kBt709}, {"srgb", color::kBt709}, {"p3d65", color::kDisplayP3},
            {"rec2020", color::kBt2020}, {"adobergb", color::kAdobeRgb}, {"ap0", color::kAcesAp0},
            {"ap1", color::kAcesAp1}};
        if (transfers.contains(parts[0]) && primaries.contains(parts[1])) {
            d->transfer = transfers.value(parts[0]);
            d->primaries = primaries.value(parts[1]);
            return true;
        }
    }
    if (n == "linear" || n == "scene_linear" || n == "lin_srgb" || n == "lin_rec709") {
        d->transfer = Transfer::Linear;
        d->primaries = color::kBt709;
        return true;
    }
    if (n == "srgb") {
        d->transfer = Transfer::Srgb;
        d->primaries = color::kBt709;
        return true;
    }
    if (n == "rec709") {
        d->transfer = Transfer::Bt1886;
        d->primaries = color::kBt709;
        return true;
    }
    if (n == "acescg") {
        d->transfer = Transfer::Linear;
        d->primaries = color::kAcesAp1;
        return true;
    }
    if (n == "aces2065-1" || n == "aces") {
        d->transfer = Transfer::Linear;
        d->primaries = color::kAcesAp0;
        return true;
    }
    if (n.startsWith("gammacorrected")) {
        bool ok = false;
        const double g = n.mid(14).toDouble(&ok);
        if (ok && std::abs(g - 2.2) < 0.05) {
            d->transfer = Transfer::Gamma22;
            d->primaries = color::kBt709;
            return true;
        }
    }
    return false;
}

// Fills `d` from the metadata OpenImageIO attached to the image.
void describeOiio(const OIIO::ImageSpec &spec, bool isFloat, const char *format, Descriptor *d)
{
    if (const OIIO::ParamValue *p = spec.find_attribute("ICCProfile");
        p && p->type().basetype == OIIO::TypeDesc::UINT8 && p->type().size() > 0) {
        d->source = Descriptor::Source::Icc;
        d->icc = QByteArray(static_cast<const char *>(p->data()), qsizetype(p->type().size()));
        const QString name = color::iccDescription(d->icc);
        d->description = QStringLiteral("ICC: %1").arg(name.isEmpty() ? QStringLiteral("(sem descrição)") : name);
        return;
    }
    if (const OIIO::ParamValue *p = spec.find_attribute("CICP");
        p && p->type() == OIIO::TypeDesc(OIIO::TypeDesc::INT, 4)) {
        const int *v = static_cast<const int *>(p->data());
        Descriptor c;
        if (color::primariesFromCicp(v[0], &c.primaries) && color::transferFromCicp(v[1], &c.transfer)) {
            d->source = Descriptor::Source::Cicp;
            d->primaries = c.primaries;
            d->transfer = c.transfer;
            d->description = QStringLiteral("CICP %1/%2/%3/%4 — %5")
                                 .arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3])
                                 .arg(color::transferName(c.transfer));
            return;
        }
    }
    if (const OIIO::ParamValue *p = spec.find_attribute("chromaticities");
        p && p->type() == OIIO::TypeDesc(OIIO::TypeDesc::FLOAT, 8)) {
        const float *c = static_cast<const float *>(p->data());
        d->source = Descriptor::Source::FormatAttributes;
        d->primaries = {{c[0], c[1]}, {c[2], c[3]}, {c[4], c[5]}, {c[6], c[7]}};
        d->transfer = Transfer::Linear;
        d->description = QStringLiteral("linear, cromaticidades do ficheiro");
        return;
    }
    // PFM carries no colour metadata and is linear by convention (HDR radiance maps), but
    // OIIO 2.4 labels every PNM variant "Rec709"; decoding that as BT.1886 turned 36.0
    // into 5434 (found by tests/render_test.py).
    const bool floatPnm = isFloat && std::strcmp(format, "pnm") == 0;
    const QString cs = floatPnm ? QString() : QString::fromStdString(spec.get_string_attribute("oiio:ColorSpace"));
    if (parseOiioColorSpace(cs, d)) {
        // OIIO also fills this in when the file carries no colour tag at all, so it
        // is reported as the decoder's interpretation, not as file metadata (F12).
        d->source = Descriptor::Source::FormatAttributes;
        d->description = QStringLiteral("%1 (atribuído pelo descodificador)").arg(cs);
        return;
    }
    d->source = Descriptor::Source::Assumed;
    d->primaries = color::kBt709;
    d->transfer = isFloat ? Transfer::Linear : Transfer::Srgb;
    d->description = isFloat ? QStringLiteral("linear BT.709 (assumido)") : QStringLiteral("sRGB (assumido)");
}

bool decodeWithOiio(const QString &path, Decoded *out, QString *error)
{
    auto in = OIIO::ImageInput::open(path.toUtf8().toStdString());
    if (!in) {
        *error = QString::fromStdString(OIIO::geterror());
        return false;
    }
    const OIIO::ImageSpec &spec = in->spec();
    const int w = spec.width, h = spec.height, nch = spec.nchannels;
    if (w <= 0 || h <= 0 || nch <= 0 || qint64(w) * h > kMaxPixels) {
        *error = QStringLiteral("dimensões inválidas (%1×%2×%3)").arg(w).arg(h).arg(nch);
        return false;
    }
    using Sample = Decoded::Sample;
    const OIIO::TypeDesc stored = spec.format;
    out->sample = stored == OIIO::TypeDesc::UINT8 ? Sample::U8
                  : (stored == OIIO::TypeDesc::UINT16 || stored == OIIO::TypeDesc::INT8
                     || stored == OIIO::TypeDesc::INT16) ? Sample::U16
                                                         : Sample::F32;
    const OIIO::TypeDesc request = out->sample == Sample::U8    ? OIIO::TypeDesc::UINT8
                                   : out->sample == Sample::U16 ? OIIO::TypeDesc::UINT16
                                                                : OIIO::TypeDesc::FLOAT;
    out->data.resize(std::size_t(w) * h * nch * out->sampleBytes());
    if (!in->read_image(0, 0, 0, nch, request, out->data.data())) {
        *error = QString::fromStdString(in->geterror());
        return false;
    }

    out->width = w;
    out->height = h;
    out->channels = nch;
    out->alphaIndex = spec.alpha_channel >= 0 && spec.alpha_channel < nch ? spec.alpha_channel : -1;
    out->gray = nch < 3;
    out->bits = spec.get_int_attribute("oiio:BitsPerSample", int(stored.size() * 8));
    out->orientation = spec.get_int_attribute("Orientation", 1);
    out->codec = QStringLiteral("OpenImageIO/%1").arg(QString::fromUtf8(in->format_name()));
    describeOiio(spec, out->sample == Sample::F32, in->format_name(), &out->colour);
    return true;
}

bool decodeWithQt(const QString &path, Decoded *out, QString *error)
{
    QImageReader reader(path);
    reader.setAutoTransform(true); // orientation is applied by Qt
    QImage image = reader.read();
    if (image.isNull()) {
        *error = reader.errorString();
        return false;
    }
    const QColorSpace cs = image.colorSpace();
    const bool deep = image.depth() > 32;
    out->width = image.width();
    out->height = image.height();
    out->sample = deep ? Decoded::Sample::U16 : Decoded::Sample::U8;
    out->channels = 4;
    out->alphaIndex = image.hasAlphaChannel() ? 3 : -1;
    out->gray = false;
    out->bits = deep ? 16 : 8;
    out->orientation = 1;
    out->codec = QStringLiteral("Qt/%1").arg(QString::fromLatin1(reader.format()));
    if (cs.isValid() && !cs.iccProfile().isEmpty()) {
        out->colour.source = Descriptor::Source::Icc;
        out->colour.icc = cs.iccProfile();
        out->colour.description = QStringLiteral("ICC: %1").arg(cs.description());
    } else {
        out->colour.description = QStringLiteral("sRGB (assumido)");
    }
    image.setColorSpace(QColorSpace()); // keep the encoded values untouched
    image.convertTo(deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888); // straight alpha
    const std::size_t rowBytes = std::size_t(out->width) * 4 * out->sampleBytes();
    out->data.resize(rowBytes * out->height);
    for (int y = 0; y < out->height; ++y)
        std::memcpy(out->data.data() + rowBytes * y, image.constScanLine(y), rowBytes);
    return true;
}

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

// Expands native samples of pixels [begin, end) into straight-alpha RGBA floats in [0, 1] (or float range).
void expand(const Decoded &dec, std::size_t begin, std::size_t end, float *dst)
{
    const int nch = dec.channels;
    auto sample = [&dec](std::size_t index) -> float {
        switch (dec.sample) {
        case Decoded::Sample::U8: return dec.data[index] / 255.0f;
        case Decoded::Sample::U16: {
            quint16 v;
            std::memcpy(&v, dec.data.data() + index * 2, 2);
            return v / 65535.0f;
        }
        case Decoded::Sample::F32: {
            float v;
            std::memcpy(&v, dec.data.data() + index * 4, 4);
            return v;
        }
        }
        return 0.0f;
    };
    for (std::size_t i = begin; i < end; ++i, dst += 4) {
        const std::size_t base = i * std::size_t(nch);
        if (dec.gray) {
            dst[0] = dst[1] = dst[2] = sample(base);
        } else {
            dst[0] = sample(base);
            dst[1] = sample(base + 1);
            dst[2] = sample(base + 2);
        }
        dst[3] = dec.alphaIndex >= 0 ? sample(base + std::size_t(dec.alphaIndex)) : 1.0f;
    }
}

// Rotates/mirrors a half-float RGBA buffer according to an EXIF orientation (2..8).
std::vector<qfloat16> orient(const std::vector<qfloat16> &src, int &w, int &h, int o)
{
    const int sw = w, sh = h;
    const bool swap = o >= 5;
    const int ow = swap ? sh : sw, oh = swap ? sw : sh;
    std::vector<qfloat16> out(src.size());
    forEachRange(std::size_t(oh), 64, [&](const Range &rows) {
        for (int oy = int(rows.begin); oy < int(rows.end); ++oy) {
            for (int ox = 0; ox < ow; ++ox) {
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

} // namespace

Image decodeImage(const QString &path, int maxTextureSize)
{
    QElapsedTimer timer;
    timer.start();
    Image result;
    result.path = path;

    Decoded dec;
    QString oiioError, qtError;
    if (!decodeWithOiio(path, &dec, &oiioError) && !decodeWithQt(path, &dec, &qtError)) {
        result.error = QStringLiteral("Não foi possível descodificar: %1").arg(oiioError.isEmpty() ? qtError : oiioError);
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
        dec.colour.description = QStringLiteral("sRGB (assumido: %1)").arg(reason);
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
    dec.data = {}; // release native samples early
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
    result.sourceChannels = dec.channels;
    result.sourceBits = dec.bits;
    result.sourceFloat = dec.sample == Decoded::Sample::F32;
    result.hasAlpha = dec.alphaIndex >= 0;
    result.orientation = dec.orientation;
    result.colour = dec.colour;
    result.maxComponent = maxComponent;
    result.maxLuminance = maxLuminance;
    result.pixels = std::move(pixels);
    result.decodeMs = double(timer.nsecsElapsed()) / 1e6;
    qCInfo(lcDecode).nospace() << QFileInfo(path).fileName() << ": read " << readNs / 1000000 << " ms, convert "
                               << (convertNs - readNs) / 1000000 << " ms, orient/downscale "
                               << (timer.nsecsElapsed() - convertNs) / 1000000 << " ms";
    return result;
}

const QStringList &supportedSuffixes()
{
    static const QStringList list = [] {
        QSet<QString> set;
        // "fmt:ext,ext;fmt:ext" — every extension the linked OpenImageIO can read.
        const QString oiio = QString::fromStdString(OIIO::get_string_attribute("extension_list"));
        for (const QString &entry : oiio.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const int colon = entry.indexOf(QLatin1Char(':'));
            for (const QString &ext : entry.mid(colon + 1).split(QLatin1Char(','), Qt::SkipEmptyParts))
                set.insert(ext.toLower());
        }
        for (const QByteArray &fmt : QImageReader::supportedImageFormats())
            set.insert(QString::fromLatin1(fmt).toLower());
        // Video containers are out of scope (decision D-P06); "null" is OIIO's test plugin.
        for (const char *excluded : {"null", "nul", "avi", "mov", "qt", "mp4", "m4a", "m4v", "3gp", "3g2", "mj2",
                                     "mkv", "webm", "mxf", "wmv", "flv", "ogv", "mpg", "mpeg", "ts", "term"})
            set.remove(QString::fromLatin1(excluded));
        QStringList sorted(set.begin(), set.end());
        sorted.sort();
        return sorted;
    }();
    return list;
}
