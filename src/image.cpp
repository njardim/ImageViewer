#include "image.h"

#include <OpenImageIO/imageio.h>

#include <QColorSpace>
#include <QElapsedTimer>
#include <QHash>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

using color::Descriptor;
using color::Transfer;

// Decoder output before colour conversion: straight-alpha RGBA floats.
struct Decoded {
    int width = 0;
    int height = 0;
    std::vector<float> rgba;
    int channels = 0;
    int bits = 0;          // significant bits per sample
    int containerBits = 0; // bits of the integer type that held the samples
    bool isFloat = false;
    bool hasAlpha = false;
    int orientation = 1;
    Descriptor colour;
    QString codec;
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
void describeOiio(const OIIO::ImageSpec &spec, bool isFloat, Descriptor *d)
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
    const QString cs = QString::fromStdString(spec.get_string_attribute("oiio:ColorSpace"));
    if (parseOiioColorSpace(cs, d)) {
        d->source = Descriptor::Source::FormatAttributes;
        d->description = QStringLiteral("%1 (indicado pelo formato)").arg(cs);
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
    std::vector<float> buf(std::size_t(w) * h * nch);
    if (!in->read_image(0, 0, 0, nch, OIIO::TypeDesc::FLOAT, buf.data())) {
        *error = QString::fromStdString(in->geterror());
        return false;
    }

    const int alpha = spec.alpha_channel >= 0 && spec.alpha_channel < nch ? spec.alpha_channel : -1;
    const bool gray = nch < 3 || (nch == 2 && alpha == 1);
    out->width = w;
    out->height = h;
    out->channels = nch;
    out->isFloat = spec.format.is_floating_point();
    out->containerBits = int(spec.format.size() * 8);
    out->bits = spec.get_int_attribute("oiio:BitsPerSample", out->containerBits);
    out->hasAlpha = alpha >= 0;
    out->orientation = spec.get_int_attribute("Orientation", 1);
    out->codec = QStringLiteral("OpenImageIO/%1").arg(QString::fromUtf8(in->format_name()));
    describeOiio(spec, out->isFloat, &out->colour);

    out->rgba.resize(std::size_t(w) * h * 4);
    const float *s = buf.data();
    float *d = out->rgba.data();
    for (std::size_t i = 0, n = std::size_t(w) * h; i < n; ++i, s += nch, d += 4) {
        if (gray) {
            d[0] = d[1] = d[2] = s[0];
        } else {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
        d[3] = alpha >= 0 ? s[alpha] : 1.0f;
    }
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
    out->width = image.width();
    out->height = image.height();
    out->channels = image.hasAlphaChannel() ? 4 : 3;
    out->bits = out->containerBits = image.depth() >= 64 ? 16 : 8;
    out->isFloat = false;
    out->hasAlpha = image.hasAlphaChannel();
    out->orientation = 1;
    out->codec = QStringLiteral("Qt/%1").arg(QString::fromLatin1(reader.format()));
    if (cs.isValid() && !cs.iccProfile().isEmpty()) {
        out->colour.source = Descriptor::Source::Icc;
        out->colour.icc = cs.iccProfile();
        out->colour.description = QStringLiteral("ICC: %1").arg(cs.description());
    } else {
        out->colour.description = QStringLiteral("sRGB (assumido)");
    }
    image.setColorSpace(QColorSpace()); // keep the raw encoded values
    image.convertTo(QImage::Format_RGBA32FPx4);
    out->rgba.resize(std::size_t(out->width) * out->height * 4);
    for (int y = 0; y < out->height; ++y) {
        const float *line = reinterpret_cast<const float *>(image.constScanLine(y));
        std::copy(line, line + std::size_t(out->width) * 4, out->rgba.data() + std::size_t(y) * out->width * 4);
    }
    return true;
}

// Applies an EXIF orientation (1..8) so that the buffer is upright.
void applyOrientation(Decoded *img)
{
    const int o = img->orientation;
    if (o <= 1 || o > 8)
        return;
    const int w = img->width, h = img->height;
    const bool swap = o >= 5;
    const int ow = swap ? h : w, oh = swap ? w : h;
    std::vector<float> out(img->rgba.size());
    for (int oy = 0; oy < oh; ++oy) {
        for (int ox = 0; ox < ow; ++ox) {
            int sx = ox, sy = oy;
            switch (o) {
            case 2: sx = w - 1 - ox; sy = oy; break;
            case 3: sx = w - 1 - ox; sy = h - 1 - oy; break;
            case 4: sx = ox; sy = h - 1 - oy; break;
            case 5: sx = oy; sy = ox; break;
            case 6: sx = oy; sy = h - 1 - ox; break;
            case 7: sx = w - 1 - oy; sy = h - 1 - ox; break;
            case 8: sx = w - 1 - oy; sy = ox; break;
            }
            const float *s = img->rgba.data() + (std::size_t(sy) * w + sx) * 4;
            std::copy(s, s + 4, out.data() + (std::size_t(oy) * ow + ox) * 4);
        }
    }
    img->rgba.swap(out);
    img->width = ow;
    img->height = oh;
}

// Box-filters a premultiplied linear buffer by an integer factor.
void downscale(std::vector<float> &rgba, int &w, int &h, int factor)
{
    const int ow = (w + factor - 1) / factor, oh = (h + factor - 1) / factor;
    std::vector<float> out(std::size_t(ow) * oh * 4, 0.0f);
    for (int oy = 0; oy < oh; ++oy) {
        for (int ox = 0; ox < ow; ++ox) {
            double acc[4] = {0, 0, 0, 0};
            int n = 0;
            for (int y = oy * factor; y < std::min(h, (oy + 1) * factor); ++y)
                for (int x = ox * factor; x < std::min(w, (ox + 1) * factor); ++x, ++n) {
                    const float *p = rgba.data() + (std::size_t(y) * w + x) * 4;
                    for (int c = 0; c < 4; ++c)
                        acc[c] += p[c];
                }
            float *d = out.data() + (std::size_t(oy) * ow + ox) * 4;
            for (int c = 0; c < 4; ++c)
                d[c] = float(acc[c] / n);
        }
    }
    rgba.swap(out);
    w = ow;
    h = oh;
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

    QString colourError;
    // Integer samples normalised to their container are exact multiples of 1/(2^n - 1).
    const int integerBits = dec.isFloat || dec.containerBits > 16 ? 0 : dec.containerBits;
    if (!color::toLinearScRgb(dec.rgba.data(), dec.rgba.size() / 4, dec.colour, integerBits, &colourError)) {
        // An unusable embedded profile must not block viewing; fall back visibly.
        dec.colour = Descriptor();
        dec.colour.description = QStringLiteral("sRGB (assumido: %1)").arg(colourError);
        color::toLinearScRgb(dec.rgba.data(), dec.rgba.size() / 4, dec.colour, integerBits, &colourError);
    }
    applyOrientation(&dec);

    // Sanitize, premultiply and measure in working units.
    constexpr float kHalfMax = 65504.0f;
    float maxComponent = 0.0f;
    for (std::size_t i = 0, n = dec.rgba.size(); i < n; i += 4) {
        float *p = dec.rgba.data() + i;
        const float a = std::isfinite(p[3]) ? std::clamp(p[3], 0.0f, 1.0f) : 1.0f;
        p[3] = a;
        for (int c = 0; c < 3; ++c) {
            const float v = std::isfinite(p[c]) ? std::clamp(p[c], -kHalfMax, kHalfMax) : 0.0f;
            maxComponent = std::max(maxComponent, v);
            p[c] = v * a;
        }
    }

    result.sourceWidth = dec.width;
    result.sourceHeight = dec.height;
    const int longest = std::max(dec.width, dec.height);
    if (maxTextureSize > 0 && longest > maxTextureSize)
        downscale(dec.rgba, dec.width, dec.height, (longest + maxTextureSize - 1) / maxTextureSize);

    result.width = dec.width;
    result.height = dec.height;
    result.codec = dec.codec;
    result.sourceChannels = dec.channels;
    result.sourceBits = dec.bits;
    result.sourceFloat = dec.isFloat;
    result.hasAlpha = dec.hasAlpha;
    result.orientation = dec.orientation;
    result.colour = dec.colour;
    result.maxComponent = maxComponent;
    result.pixels.resize(dec.rgba.size());
    qFloatToFloat16(result.pixels.data(), dec.rgba.data(), qsizetype(dec.rgba.size()));
    result.decodeMs = double(timer.nsecsElapsed()) / 1e6;
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
