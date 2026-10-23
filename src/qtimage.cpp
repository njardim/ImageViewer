// What Qt decodes for the viewer (decisions D-67, D-68, D-71): the formats the registry gives
// Qt (SVG, Apple icons, cursors, XPM, XBM, WBMP), each only through its own plugin; an SVG
// only from memory, after everything that reaches outside the document is taken out of it;
// and the PNG, JPEG, GIF and WebP images those files embed, decoded by OpenImageIO through a
// Qt image plugin built into the executable. Split from decoders.cpp and codecs.cpp (D-08).
#define QT_STATICPLUGIN // the plugin's entry point for Q_IMPORT_PLUGIN (main.cpp), not a library's

#include "decoders.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageIOHandler>
#include <QImageIOPlugin>
#include <QImageReader>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

using color::Descriptor;
using color::Transfer;

constexpr qsizetype kMaxSvgBytes = qsizetype(256) << 20; // the XML of one SVG document, inflated
constexpr int kMaxSvgNesting = 4;                         // SVG images inside SVG images

// An svgz file's XML, every gzip member inflated, within kMaxSvgBytes: a few kilobytes can
// inflate to gigabytes, which Qt's own reader would allocate. Empty when damaged or larger.
QByteArray gunzip(QByteArrayView in)
{
    z_stream z{};
    if (inflateInit2(&z, MAX_WBITS + 16) != Z_OK) // + 16: a gzip header and trailer
        return {};
    z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.data()));
    z.avail_in = uInt(in.size()); // at most 2 GiB (readWholeFile)
    QByteArray out;
    int status = Z_OK;
    for (;;) {
        if (status == Z_STREAM_END) {
            if (z.avail_in == 0)
                break;
            if (inflateReset(&z) != Z_OK) { // another member follows
                status = Z_DATA_ERROR;
                break;
            }
        }
        if (out.size() >= kMaxSvgBytes) {
            status = Z_MEM_ERROR;
            break;
        }
        const qsizetype at = out.size();
        out.resize(std::min(kMaxSvgBytes, std::max<qsizetype>(2 * at, 1 << 16)));
        z.next_out = reinterpret_cast<Bytef *>(out.data() + at);
        z.avail_out = uInt(out.size() - at);
        status = inflate(&z, Z_NO_FLUSH);
        out.resize(out.size() - qsizetype(z.avail_out));
        if (status != Z_OK && status != Z_STREAM_END) // a damaged or truncated stream
            break;
    }
    inflateEnd(&z);
    return status == Z_STREAM_END ? out : QByteArray();
}

QByteArray safeSvg(QByteArrayView svg, int nesting);

// A data: URL of an SVG's <image> (RFC 2397, "data:[<type>][;base64],<data>"), written again
// from the bytes decoded here, so that Qt decodes exactly what was checked: an embedded PNG,
// JPEG, GIF or WebP, or an SVG made safe in turn; empty for anything else.
QString safeDataUrl(QStringView url, int nesting)
{
    const qsizetype comma = url.indexOf(QLatin1Char(','));
    if (comma < 0)
        return {};
    QByteArray bytes = QByteArray::fromPercentEncoding(url.sliced(comma + 1).toUtf8());
    if (url.first(comma).endsWith(QLatin1String(";base64"), Qt::CaseInsensitive))
        bytes = QByteArray::fromBase64(bytes); // lenient, as Qt's: what it decodes is what is written below
    if (const char *format = embeddedImageFormat(bytes))
        return QStringLiteral("data:image/%1;base64,").arg(QLatin1String(format)) + QString::fromLatin1(bytes.toBase64());
    if (nesting >= kMaxSvgNesting)
        return {};
    const QByteArray svg = safeSvg(bytes, nesting + 1);
    return svg.isEmpty() ? QString() : QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(svg.toBase64());
}

// The SVG document Qt is given (D-71), read and written again with nothing that makes Qt's SVG
// renderer read outside it, as browsers show an SVG used as an image. Qt's renderer opens the
// file an <image> names (beside the SVG, at any path, or on Windows \\server\share, where the
// system connects and presents the user's credentials) and the style sheet an xml-stylesheet
// instruction names [code: qtsvg v6.12.0 qsvghandler.cpp:1967-1981, 4238-4262]. So no DTD (its
// entities are expanded by the reader here, and an ATTLIST default would put back an attribute
// taken out), no processing instruction, and no reference but "#id": an <image> keeps only an
// embedded image (safeDataUrl). Empty when the document cannot be read or is too large.
// Only the file itself may be an svgz: an SVG inside it inflated as well would multiply the work
// at each level (a few kilobytes of gzip inflate to hundreds of megabytes, which can hold many
// more), and browsers do not inflate a data: URL either.
QByteArray safeSvg(QByteArrayView svg, int nesting)
{
    const QByteArray xml = nesting == 0 && svg.startsWith("\x1f\x8b") ? gunzip(svg) // svgz
                           : svg.size() <= kMaxSvgBytes ? QByteArray::fromRawData(svg.data(), svg.size()) // read in place
                                                        : QByteArray();
    if (xml.isEmpty())
        return {};
    QXmlStreamReader reader(xml);
    reader.setNamespaceProcessing(false); // names as written, as Qt's renderer reads them
    QByteArray out;
    QXmlStreamWriter writer(&out);
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartDocument: writer.writeStartDocument(); break;
        case QXmlStreamReader::EndDocument: writer.writeEndDocument(); break;
        case QXmlStreamReader::StartElement: {
            const QStringView name = reader.qualifiedName();
            const bool image = name.sliced(name.lastIndexOf(QLatin1Char(':')) + 1) == QLatin1String("image");
            writer.writeStartElement(name.toString());
            const QXmlStreamAttributes attributes = reader.attributes();
            for (const QXmlStreamAttribute &attribute : attributes) {
                const QStringView key = attribute.qualifiedName();
                QString value = attribute.value().toString();
                if (key == QLatin1String("href") || key.endsWith(QLatin1String(":href"))) {
                    const QStringView reference = QStringView(value).trimmed();
                    if (image)
                        value = reference.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)
                                    ? safeDataUrl(reference, nesting)
                                    : QString();
                    else if (!reference.startsWith(QLatin1Char('#')))
                        value.clear();
                    if (value.isEmpty())
                        continue;
                }
                writer.writeAttribute(key.toString(), value);
            }
            break;
        }
        case QXmlStreamReader::EndElement: writer.writeEndElement(); break;
        case QXmlStreamReader::Characters:
            if (reader.isCDATA())
                writer.writeCDATA(reader.text().toString());
            else
                writer.writeCharacters(reader.text().toString());
            break;
        default: break; // the DTD, processing instructions, entity references the reader could not expand, comments
        }
        if (out.size() > kMaxSvgBytes)
            return {};
    }
    return reader.hasError() || writer.hasError() ? QByteArray() : out;
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
    const bool svg = std::strcmp(format->id, "svg") == 0;
    // Qt's SVG reader lays out text with QFontDatabase, which aborts without a QGuiApplication
    // (console modes such as --info run on a QCoreApplication).
    if (svg && !qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        *error = QCoreApplication::translate("Image", "SVG is only decoded in the graphical interface");
        return false;
    }
    QByteArray document; // an SVG, made safe (D-71), read from memory
    QBuffer buffer(&document);
    QImageReader reader;
    if (svg) {
        QByteArray file;
        if (!readWholeFile(path, &file, error))
            return false;
        document = safeSvg(file, 0);
        if (document.isEmpty()) {
            qCInfo(lcDecode).noquote() << "SVG: not well-formed XML, or more than" << (kMaxSvgBytes >> 20) << "MiB:" << path;
            *error = damaged(format->name);
            return false;
        }
        buffer.open(QIODevice::ReadOnly);
        reader.setDevice(&buffer);
        reader.setFormat("svg");
    } else {
        const QStringList suffixes = format->suffixes();
        const QString suffix = QFileInfo(path).suffix().toLower();
        reader.setFileName(path);
        reader.setFormat((suffixes.contains(suffix) ? suffix : suffixes.constFirst()).toLatin1());
    }
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

// Qt decodes the images inside the files it reads for us (an SVG's data: URIs, cursors, Apple
// icons) with its own copies of libpng, libjpeg and libwebp, which the dependency audit cannot
// follow and which lag the ones we ship (Qt 6.12.0: libpng 1.6.58, without the fix of
// CVE-2026-46675); the packages carry none of Qt's other image plugins. Qt asks a plugin that
// names the format before its own reader, for a named format and by content alike: this handler
// decodes PNG, JPEG, GIF and WebP with OpenImageIO, through the libraries the audit follows
// (D-68, D-71). The colour Qt's own reader would attach to the image: decodeWithQt() reads it
// back for cursors and icons (an SVG's painter ignores it).
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
    static bool canRead(QIODevice *device) { return device && embeddedImageFormat(device->peek(12)) != nullptr; }

    bool canRead() const override
    {
        const char *format = device() ? embeddedImageFormat(device()->peek(12)) : nullptr;
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
        const char *detected = device ? embeddedImageFormat(device->peek(12)) : nullptr;
        handler->setFormat(format.isEmpty() && detected ? QByteArray(detected) : format);
        return handler;
    }
};

#include "qtimage.moc"
