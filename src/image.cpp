#include "image.h"

#include <OpenImageIO/imageio.h>

#include <QColorSpace>
#include <QElapsedTimer>
#include <QHash>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
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

// Decoder output before colour conversion: interleaved samples in their native type.
struct Decoded {
    enum class Sample { U8, U16, F16, F32 };
    int width = 0;
    int height = 0;
    Sample sample = Sample::U8;
    int channels = 0;              // interleaved channels in `data`
    int sourceChannels = 0;        // channels in the file (informative)
    int alphaIndex = -1;           // channel holding alpha, -1 if none
    bool associatedAlpha = false;  // colour already multiplied by alpha (EXR, some TIFFs)
    bool gray = false;             // channel 0 is luminance
    std::unique_ptr<unsigned char[]> data; // not zero-filled: every byte is written by the reader
    int bits = 0;                  // significant bits per sample (informative)
    int orientation = 1;
    Descriptor colour;
    QString codec;

    int sampleBytes() const
    {
        switch (sample) {
        case Sample::U8: return 1;
        case Sample::U16:
        case Sample::F16: return 2;
        case Sample::F32: return 4;
        }
        return 4;
    }
    bool isInteger() const { return sample == Sample::U8 || sample == Sample::U16; }
};

constexpr qint64 kMaxPixels = qint64(1) << 30; // refuse absurd dimensions before anything else
constexpr qint64 kWorkingBytesPerPixel = 16;    // RGBA16F result plus one orient/downscale copy

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

// Refuses images whose decode would need more than ~60 % of physical memory:
// failing early with a message beats swapping the machine or an OOM kill.
bool fitsInMemory(qint64 pixels, int nativeBytesPerPixel, QString *error)
{
    static const qint64 budget = physicalMemoryBytes() / 10 * 6;
    const qint64 needed = pixels * (nativeBytesPerPixel + kWorkingBytesPerPixel);
    if (budget <= 0 || needed <= budget)
        return true;
    *error = QStringLiteral("imagem demasiado grande para a memória (precisa de %1 GB, limite %2 GB)")
                 .arg(double(needed) / (1 << 30), 0, 'f', 1)
                 .arg(double(budget) / (1 << 30), 0, 'f', 1);
    return false;
}

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
            {"g24", Transfer::Bt1886}, {"pq", Transfer::Pq}, {"hlg", Transfer::Hlg}};
        static const QHash<QString, color::Chromaticities> primaries = {
            {"rec709", color::kBt709}, {"srgb", color::kBt709}, {"p3d65", color::kDisplayP3},
            {"rec2020", color::kBt2020}, {"adobergb", color::kAdobeRgb}, {"ap0", color::kAcesAp0},
            {"ap1", color::kAcesAp1}};
        static const QRegularExpression powerId(QStringLiteral("^g(\\d{2})$")); // g18, g22, g26: gamma x 10
        const QRegularExpressionMatch power = powerId.match(parts[0]);
        if (primaries.contains(parts[1]) && (transfers.contains(parts[0]) || power.hasMatch())) {
            d->primaries = primaries.value(parts[1]);
            if (transfers.contains(parts[0])) {
                d->transfer = transfers.value(parts[0]);
            } else {
                d->transfer = Transfer::Power;
                d->gamma = power.captured(1).toFloat() / 10.0f;
            }
            return true;
        }
    }
    d->primaries = color::kBt709;
    if (n == "linear" || n == "scene_linear" || n == "lin_srgb" || n == "lin_rec709") {
        d->transfer = Transfer::Linear;
        return true;
    }
    if (n == "srgb") {
        d->transfer = Transfer::Srgb;
        return true;
    }
    if (n == "rec709") {
        d->transfer = Transfer::Bt1886;
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
    // OIIO 2.4 reports PNG gAMA and similar as "Gamma2.2" / "GammaCorrected2.2".
    static const QRegularExpression gammaName(QStringLiteral("^gamma(?:corrected)?(\\d+(?:\\.\\d+)?)$"));
    if (const QRegularExpressionMatch m = gammaName.match(n); m.hasMatch()) {
        const float g = m.captured(1).toFloat();
        if (g > 0.5f && g < 5.0f) {
            d->transfer = Transfer::Power;
            d->gamma = g;
            return true;
        }
    }
    return false;
}

// Fills `d` from the metadata OpenImageIO attached to the image.
void describeOiio(const OIIO::ImageSpec &spec, bool isFloat, const char *format, Descriptor *d)
{
    // CICP first: HDR JPEG XL and PNG (3rd edition) carry it next to an ICC profile,
    // which for PQ/HLG is only an SDR approximation; the PNG specification gives cICP
    // precedence over iCCP.
    if (const OIIO::ParamValue *p = spec.find_attribute("CICP");
        p && p->type() == OIIO::TypeDesc(OIIO::TypeDesc::INT, 4)) {
        const int *v = static_cast<const int *>(p->data());
        Descriptor c;
        if (color::primariesFromCicp(v[0], &c.primaries) && color::transferFromCicp(v[1], &c.transfer, &c.gamma)) {
            d->source = Descriptor::Source::Cicp;
            d->primaries = c.primaries;
            d->transfer = c.transfer;
            d->gamma = c.gamma;
            d->fullRange = v[3] != 0;
            d->description = QStringLiteral("CICP %1/%2/%3/%4 — %5")
                                 .arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3])
                                 .arg(color::transferName(c.transfer, c.gamma));
            return;
        }
    }
    if (const OIIO::ParamValue *p = spec.find_attribute("ICCProfile");
        p && p->type().basetype == OIIO::TypeDesc::UINT8 && p->type().size() > 0) {
        d->source = Descriptor::Source::Icc;
        d->icc = QByteArray(static_cast<const char *>(p->data()), qsizetype(p->type().size()));
        const QString name = color::iccDescription(d->icc);
        d->description = QStringLiteral("ICC: %1").arg(name.isEmpty() ? QStringLiteral("(sem descrição)") : name);
        return;
    }
    if (const OIIO::ParamValue *p = spec.find_attribute("chromaticities");
        p && p->type() == OIIO::TypeDesc(OIIO::TypeDesc::FLOAT, 8)) {
        const float *c = static_cast<const float *>(p->data());
        const color::Chromaticities chroma = {{c[0], c[1]}, {c[2], c[3]}, {c[4], c[5]}, {c[6], c[7]}};
        if (color::isUsable(chroma)) {
            d->source = Descriptor::Source::FormatAttributes;
            d->primaries = chroma;
            d->transfer = Transfer::Linear;
            d->description = QStringLiteral("linear, cromaticidades do ficheiro");
            return;
        }
        d->description = QStringLiteral("linear BT.709 (assumido: cromaticidades do ficheiro inválidas)");
        d->source = Descriptor::Source::Assumed;
        d->primaries = color::kBt709;
        d->transfer = Transfer::Linear;
        return;
    }
    // PFM carries no colour metadata and is linear by convention (HDR radiance maps), but
    // OIIO labels every PNM variant "Rec709"; decoding that as BT.1886 turned 36.0 into 5434.
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
    OIIO::ImageSpec config;
    // Straight alpha where the format stores it (PNG, TIFF, WebP, HEIF...): OIIO would
    // otherwise premultiply the encoded values, which is wrong for non-linear data.
    config.attribute("oiio:UnassociatedAlpha", 1);
    // PFM stores rows bottom to top; OIIO 3.1 flips them only when asked (pnminput.cpp).
    config.attribute("pnm:pfmflip", 1);
    auto in = OIIO::ImageInput::open(path.toUtf8().toStdString(), &config);
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
    switch (stored.basetype) {
    case OIIO::TypeDesc::UINT8: out->sample = Sample::U8; break;
    case OIIO::TypeDesc::INT8:
    case OIIO::TypeDesc::UINT16:
    case OIIO::TypeDesc::INT16:
    case OIIO::TypeDesc::UINT32: // integer semantics (and transfer); 16 bits are plenty for display
    case OIIO::TypeDesc::INT32: out->sample = Sample::U16; break;
    case OIIO::TypeDesc::HALF: out->sample = Sample::F16; break;
    default: out->sample = Sample::F32; break;
    }
    const OIIO::TypeDesc request = out->sample == Sample::U8    ? OIIO::TypeDesc::UINT8
                                   : out->sample == Sample::U16 ? OIIO::TypeDesc::UINT16
                                   : out->sample == Sample::F16 ? OIIO::TypeDesc::HALF
                                                                : OIIO::TypeDesc::FLOAT;
    // Only colour and alpha are read: an EXR with 40 AOV channels must not cost 10x the memory.
    const int alpha = spec.alpha_channel >= 0 && spec.alpha_channel < nch ? spec.alpha_channel : -1;
    const bool gray = nch < 3;
    const int readChannels = std::max(gray ? 1 : 3, alpha + 1);
    const qint64 pixels = qint64(w) * h;
    if (!fitsInMemory(pixels, readChannels * out->sampleBytes(), error))
        return false;
    out->data.reset(new unsigned char[std::size_t(pixels) * std::size_t(readChannels * out->sampleBytes())]);
    if (!in->read_image(0, 0, 0, readChannels, request, out->data.get())) {
        *error = QString::fromStdString(in->geterror());
        return false;
    }

    out->width = w;
    out->height = h;
    out->channels = readChannels;
    out->sourceChannels = nch;
    out->alphaIndex = alpha;
    out->associatedAlpha = alpha >= 0 && spec.get_int_attribute("oiio:UnassociatedAlpha", 0) == 0;
    out->gray = gray;
    out->bits = spec.get_int_attribute("oiio:BitsPerSample", int(stored.size() * 8));
    out->orientation = spec.get_int_attribute("Orientation", 1);
    out->codec = QStringLiteral("OpenImageIO/%1").arg(QString::fromUtf8(in->format_name()));
    describeOiio(spec, !out->isInteger(), in->format_name(), &out->colour);
    return true;
}

// Describes a QColorSpace that was not built from an ICC profile (PNG gAMA/cHRM/sRGB chunks).
bool describeQtColorSpace(const QColorSpace &cs, Descriptor *d)
{
    switch (cs.primaries()) {
    case QColorSpace::Primaries::SRgb: d->primaries = color::kBt709; break;
    case QColorSpace::Primaries::AdobeRgb: d->primaries = color::kAdobeRgb; break;
    case QColorSpace::Primaries::DciP3D65: d->primaries = color::kDisplayP3; break;
    case QColorSpace::Primaries::Bt2020: d->primaries = color::kBt2020; break;
    default: return false;
    }
    switch (cs.transferFunction()) {
    case QColorSpace::TransferFunction::Linear: d->transfer = Transfer::Linear; break;
    case QColorSpace::TransferFunction::SRgb: d->transfer = Transfer::Srgb; break;
    case QColorSpace::TransferFunction::Gamma:
        d->transfer = Transfer::Power;
        d->gamma = cs.gamma();
        break;
    case QColorSpace::TransferFunction::Bt2020: d->transfer = Transfer::Bt1886; break; // decision D-12
    case QColorSpace::TransferFunction::St2084: d->transfer = Transfer::Pq; break;
    case QColorSpace::TransferFunction::Hlg: d->transfer = Transfer::Hlg; break;
    default: return false;
    }
    d->source = Descriptor::Source::FormatAttributes;
    d->description = QStringLiteral("%1 (metadados do ficheiro, via Qt)").arg(cs.description());
    return true;
}

bool decodeWithQt(const QString &path, Decoded *out, QString *error)
{
    QImageReader reader(path);
    // Qt's SVG reader lays out text with QFontDatabase, which aborts without a QGuiApplication
    // (console modes such as --info run on a QCoreApplication).
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())
        && (reader.format() == "svg" || reader.format() == "svgz")) {
        *error = QStringLiteral("SVG só é descodificado com a interface gráfica");
        return false;
    }
    reader.setAutoTransform(true); // orientation is applied by Qt
    QImage image = reader.read();
    if (image.isNull()) {
        *error = reader.errorString();
        return false;
    }
    if (!fitsInMemory(qint64(image.width()) * image.height(), 16, error))
        return false;
    const QColorSpace cs = image.colorSpace();
    // Keep the source precision: float formats stay float (values above 1 survive),
    // anything deeper than 8 bits per channel goes through 16 bits.
    const QPixelFormat pf = image.pixelFormat();
    const bool isFloat = pf.typeInterpretation() == QPixelFormat::FloatingPoint;
    const bool deep = !isFloat && (pf.redSize() > 8 || image.depth() / std::max<uint>(pf.channelCount(), 1) > 8);
    out->width = image.width();
    out->height = image.height();
    out->sample = isFloat ? Decoded::Sample::F32 : deep ? Decoded::Sample::U16 : Decoded::Sample::U8;
    out->channels = 4;
    out->sourceChannels = int(pf.channelCount());
    out->alphaIndex = image.hasAlphaChannel() ? 3 : -1;
    out->gray = false;
    out->bits = isFloat ? 32 : deep ? 16 : 8;
    out->orientation = 1;
    out->codec = QStringLiteral("Qt/%1").arg(QString::fromLatin1(reader.format()));
    if (cs.isValid() && !cs.iccProfile().isEmpty()) {
        out->colour.source = Descriptor::Source::Icc;
        out->colour.icc = cs.iccProfile();
        out->colour.description = QStringLiteral("ICC: %1").arg(cs.description());
    } else if (!(cs.isValid() && describeQtColorSpace(cs, &out->colour))) {
        out->colour.transfer = isFloat ? Transfer::Linear : Transfer::Srgb;
        out->colour.description = isFloat ? QStringLiteral("linear BT.709 (assumido)") : QStringLiteral("sRGB (assumido)");
    }
    image.setColorSpace(QColorSpace()); // keep the encoded values untouched
    image.convertTo(isFloat ? QImage::Format_RGBA32FPx4 : deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    const std::size_t rowBytes = std::size_t(out->width) * 4 * std::size_t(out->sampleBytes());
    out->data.reset(new unsigned char[rowBytes * std::size_t(out->height)]);
    for (int y = 0; y < out->height; ++y)
        std::memcpy(out->data.get() + rowBytes * std::size_t(y), image.constScanLine(y), rowBytes);
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

Image decode(const QString &path, int maxTextureSize)
{
    QElapsedTimer timer;
    timer.start();
    Image result;
    result.path = path;

    Decoded dec;
    QString oiioError, qtError;
    if (!decodeWithOiio(path, &dec, &oiioError)) {
        dec = Decoded(); // nothing from the failed attempt may leak into the fallback
        if (!decodeWithQt(path, &dec, &qtError)) {
            result.error = QStringLiteral("Não foi possível descodificar: %1").arg(oiioError.isEmpty() ? qtError : oiioError);
            return result;
        }
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
        dec.colour.description = QStringLiteral("%1 (assumido: %2)")
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
    result.maxComponent = maxComponent;
    result.maxLuminance = maxLuminance;
    result.pixels = std::move(pixels);
    result.decodeMs = double(timer.nsecsElapsed()) / 1e6;
    qCInfo(lcDecode).nospace() << QFileInfo(path).fileName() << ": read " << readNs / 1000000 << " ms, convert "
                               << (convertNs - readNs) / 1000000 << " ms, orient/downscale "
                               << (timer.nsecsElapsed() - convertNs) / 1000000 << " ms"
                               << (converter->usesLittleCms() ? " (LittleCMS)" : "");
    return result;
}

} // namespace

Image decodeImage(const QString &path, int maxTextureSize)
{
    // Runs on a worker thread: an exception escaping here would be rethrown by
    // QFuture::result() on the GUI thread and terminate the application.
    try {
        return decode(path, maxTextureSize);
    } catch (const std::bad_alloc &) {
        Image failed;
        failed.path = path;
        failed.error = QStringLiteral("Memória insuficiente para descodificar a imagem.");
        return failed;
    } catch (const std::exception &e) {
        Image failed;
        failed.path = path;
        failed.error = QStringLiteral("Erro ao descodificar: %1").arg(QString::fromLocal8Bit(e.what()));
        return failed;
    }
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
