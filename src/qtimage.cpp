// What Qt decodes for the viewer (decisions D-67, D-68): the formats the registry gives Qt (Apple
// icons, cursors, XPM, XBM, WBMP), each only through its own plugin; and the PNG, JPEG, GIF and
// WebP images those files embed, decoded by OpenImageIO through a Qt image plugin built into the
// executable. SVG is svg.cpp's (D-90). Split from decoders.cpp and codecs.cpp (D-08).
#define QT_STATICPLUGIN // the plugin's entry point for Q_IMPORT_PLUGIN (main.cpp), not a library's

#include "decoders.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QImageIOHandler>
#include <QImageIOPlugin>
#include <QImageReader>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

using color::Descriptor;
using color::Transfer;

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
    d->description = QCoreApplication::translate("Image", "%1 (file metadata, via Qt)").arg(cs.description());
    return true;
}

} // namespace

bool decodeWithQt(const QString &path, const Format *format, qint64 maxPixels, Decoded *out, QString *error)
{
    // Only the plugin of the format the registry gave to Qt reads the file, never one Qt picks by
    // content: Qt's other plugins and built-in readers decode with Qt's own copies of libjpeg,
    // libpng, libtiff and libwebp, which the dependency audit does not follow (D-67).
    if (!format || format->decoder != Decoder::Qt) {
        *error = QCoreApplication::translate("Image", "damaged, truncated or unsupported file");
        return false;
    }
    QImageReader reader;
    const QStringList suffixes = format->suffixes();
    const QString suffix = QFileInfo(path).suffix().toLower();
    reader.setFileName(path);
    reader.setFormat((suffixes.contains(suffix) ? suffix : suffixes.constFirst()).toLatin1());
    reader.setAutoDetectImageFormat(false);
    if (const QSize size = reader.size(); maxPixels > 0 && size.isValid() && qint64(size.width()) * size.height() > maxPixels) {
        out->overLimit = true;
        return false;
    }
    reader.setAutoTransform(true); // orientation is applied by Qt
    QImage image = reader.read();
    if (image.isNull()) {
        *error = libraryError("Qt:", reader.errorString().toStdString());
        return false;
    }
    // Again with the image read: a plugin may not know the size before (the limit asked for, too).
    if (!checkDimensions(image.width(), image.height(), 4, 16, maxPixels, out, error))
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
    if (cs.isValid() && !cs.iccProfile().isEmpty())
        describeIcc(cs.iccProfile(), &out->colour);
    else if (!(cs.isValid() && describeQtColorSpace(cs, &out->colour)))
        assumeDefault(isFloat, &out->colour);
    image.setColorSpace(QColorSpace()); // keep the encoded values untouched
    image.convertTo(isFloat ? QImage::Format_RGBA32FPx4 : deep ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    const std::size_t rowBytes = std::size_t(out->width) * 4 * std::size_t(out->sampleBytes());
    out->data.reset(new unsigned char[rowBytes * std::size_t(out->height)]);
    for (int y = 0; y < out->height; ++y)
        std::memcpy(out->data.get() + rowBytes * std::size_t(y), image.constScanLine(y), rowBytes);
    return true;
}

namespace {

// Qt decodes the images inside the files it reads for us (cursors, Apple icons) with its own
// copies of libpng, libjpeg and libwebp, which the dependency audit cannot
// follow and which lag the ones we ship (Qt 6.12.0: libpng 1.6.58, without the fix of
// CVE-2026-46675); the packages carry none of Qt's other image plugins. Qt asks a plugin that
// names the format before its own reader, for a named format and by content alike: this handler
// decodes PNG, JPEG, GIF and WebP with OpenImageIO, through the libraries the audit follows
// (D-68, D-71). The colour Qt's own reader would attach to the image: decodeWithQt() reads it
// back for cursors and icons.
QColorSpace qtColorSpace(const color::Descriptor &d)
{
    if (d.source == color::Descriptor::Source::Icc)
        return QColorSpace::fromIccProfile(d.icc);
    const color::Chromaticities &c = d.primaries, &srgb = color::kBt709;
    const auto same = [](const double a[2], const double b[2]) { return a[0] == b[0] && a[1] == b[1]; };
    if (d.source == color::Descriptor::Source::Assumed
        || (d.transfer == Transfer::Srgb && same(c.r, srgb.r) && same(c.g, srgb.g) && same(c.b, srgb.b) && same(c.w, srgb.w)))
        return {}; // what OpenImageIO calls any PNG without colour chunks: no tag, as Qt's own reader
    const auto point = [](const double xy[2]) { return QPointF(xy[0], xy[1]); };
    QColorSpace::TransferFunction function = QColorSpace::TransferFunction::Gamma;
    float gamma = 0.0f;
    switch (d.transfer) {
    case Transfer::Linear: function = QColorSpace::TransferFunction::Linear; break;
    case Transfer::Srgb: function = QColorSpace::TransferFunction::SRgb; break;
    case Transfer::Bt1886: gamma = 2.4f; break;
    case Transfer::Power: gamma = d.gamma; break;
    case Transfer::Pq: function = QColorSpace::TransferFunction::St2084; break;
    case Transfer::Hlg: function = QColorSpace::TransferFunction::Hlg; break;
    }
    QColorSpace space(point(c.w), point(c.r), point(c.g), point(c.b), function, gamma);
    space.setDescription(d.description);
    return space;
}

class OiioImageHandler final : public QImageIOHandler {
public:
    // Qt's own plugins look only at a readable device (a writer's device may be write-only).
    static bool canRead(QIODevice *device)
    {
        return device && device->isReadable() && embeddedImageFormat(device->peek(12)) != nullptr;
    }

    bool canRead() const override
    {
        const char *format = device() && device()->isReadable() ? embeddedImageFormat(device()->peek(12)) : nullptr;
        if (format)
            setFormat(format);
        return format != nullptr;
    }

    bool read(QImage *image) override
    {
        const QByteArray bytes = device()->readAll();
        const char *format = embeddedImageFormat(bytes);
        if (!format) // asked for one format, given another
            return false;
        // Within Qt's limit on the images it allocates (QImageReader::allocationLimit(), in MiB).
        const qint64 limit = QImageReader::allocationLimit() > 0 ? (qint64(QImageReader::allocationLimit()) << 20) / 8
                                                                 : kMaxPixels;
        Decoded decoded;
        QString error;
        if (!decodeOiioMemory(bytes, ("embedded." + std::string(format)).c_str(), &decoded, &error, std::min(limit, kMaxPixels))
            || (decoded.sample != Decoded::Sample::U8 && decoded.sample != Decoded::Sample::U16))
            return false;
        const bool wide = decoded.sample == Decoded::Sample::U16;
        QImage out;
        if (!allocateImage(QSize(decoded.width, decoded.height), wide ? QImage::Format_RGBA64 : QImage::Format_RGBA8888, &out))
            return false;
        // Straight RGBA at the file's depth: grey spread to the three colours, opaque without alpha.
        const std::size_t channels = std::size_t(decoded.channels), bytesPerSample = std::size_t(decoded.sampleBytes());
        const auto sample = [&](const unsigned char *pixel, int channel) -> quint16 {
            if (!wide)
                return pixel[channel];
            quint16 v;
            std::memcpy(&v, pixel + std::size_t(channel) * 2, 2);
            return v;
        };
        for (int y = 0; y < decoded.height; ++y) {
            const unsigned char *pixel = decoded.data.get() + std::size_t(y) * std::size_t(decoded.width) * channels * bytesPerSample;
            uchar *line = out.scanLine(y);
            for (int x = 0; x < decoded.width; ++x, pixel += channels * bytesPerSample) {
                const quint16 full = wide ? 65535 : 255;
                const quint16 rgba[4] = {sample(pixel, 0), sample(pixel, decoded.gray ? 0 : 1),
                                         sample(pixel, decoded.gray ? 0 : 2),
                                         decoded.alphaIndex >= 0 ? sample(pixel, decoded.alphaIndex) : full};
                if (wide)
                    std::memcpy(line + std::size_t(x) * 8, rgba, 8);
                else
                    for (int c = 0; c < 4; ++c)
                        line[std::size_t(x) * 4 + std::size_t(c)] = uchar(rgba[c]);
            }
        }
        out.setColorSpace(qtColorSpace(decoded.colour));
        *image = std::move(out);
        return true;
    }
};

} // namespace

// Imported by main.cpp (Q_IMPORT_PLUGIN): Qt's plugin loader lists it with the image plugins.
class OiioImagePlugin final : public QImageIOPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QImageIOHandlerFactoryInterface_iid FILE "imageplugin.json")

public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override
    {
        // Never CanWrite: Qt's own writer encodes what the viewer copies.
        if (format == "png" || format == "jpeg" || format == "jpg" || format == "gif" || format == "webp")
            return CanRead;
        return format.isEmpty() && OiioImageHandler::canRead(device) ? CanRead : Capabilities();
    }

    QImageIOHandler *create(QIODevice *device, const QByteArray &format) const override
    {
        auto *handler = new OiioImageHandler;
        handler->setDevice(device);
        const char *detected = device && device->isReadable() ? embeddedImageFormat(device->peek(12)) : nullptr;
        handler->setFormat(format.isEmpty() && detected ? QByteArray(detected) : format);
        return handler;
    }
};

#include "qtimage.moc"
