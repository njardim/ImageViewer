// SVG (decisions D-71, D-90): read in the decode worker only (D-38). The viewer sends the file's
// bytes; the worker inflates an svgz within kMaxSvgBytes, takes out of the document everything
// that reaches outside it (safeSvg()), and draws it with the renderer behind imageviewer_svg.h
// (resvg, through packaging/vcpkg/ports/imageviewer-svg). This is the only file that includes that
// header: another renderer is another implementation of it, and nothing here changes but its name.
#include "decoders.h"

#include <QByteArray>
#include <QString>
#include <QThread>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>

#ifdef IMAGEVIEWER_HAVE_SVG
#include <imageviewer_svg.h>
#endif

namespace {

constexpr qsizetype kMaxSvgBytes = qsizetype(256) << 20; // the XML of one SVG document, inflated
constexpr int kMaxSvgNesting = 4;                         // SVG images inside SVG images

// An svgz file's XML, every gzip member inflated, within kMaxSvgBytes: a few kilobytes can
// inflate to gigabytes. Empty when damaged or larger.
QByteArray gunzip(QByteArrayView in)
{
    z_stream z{};
    if (inflateInit2(&z, MAX_WBITS + 16) != Z_OK) // + 16: a gzip header and trailer
        return {};
    z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.data()));
    z.avail_in = uInt(in.size()); // at most kMaxFileBytes (the worker's input)
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

QByteArray safeSvg(QByteArrayView svg, int nesting, bool *hasText);

// A data: URL of an SVG's <image> (RFC 2397, "data:[<type>][;base64],<data>"), written again
// from the bytes decoded here, so that the renderer decodes exactly what was checked: an embedded
// PNG, JPEG, GIF or WebP, or an SVG made safe in turn; empty for anything else.
QString safeDataUrl(QStringView url, int nesting, bool *hasText)
{
    const qsizetype comma = url.indexOf(QLatin1Char(','));
    if (comma < 0)
        return {};
    QByteArray bytes = QByteArray::fromPercentEncoding(url.sliced(comma + 1).toUtf8());
    if (url.first(comma).endsWith(QLatin1String(";base64"), Qt::CaseInsensitive))
        bytes = QByteArray::fromBase64(bytes); // lenient: what it decodes is what is written below
    if (const char *format = embeddedImageFormat(bytes))
        return QStringLiteral("data:image/%1;base64,").arg(QLatin1String(format)) + QString::fromLatin1(bytes.toBase64());
    if (nesting >= kMaxSvgNesting)
        return {};
    const QByteArray svg = safeSvg(bytes, nesting + 1, hasText);
    return svg.isEmpty() ? QString() : QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(svg.toBase64());
}

// The SVG document the renderer is given (D-71), read and written again with nothing that would
// make a renderer read outside it, as browsers show an SVG used as an image: no DTD (its entities
// are expanded by the reader here, and an ATTLIST default would put back an attribute taken out),
// no processing instruction (xml-stylesheet), and no reference but "#id": an <image> keeps only an
// embedded image (safeDataUrl). Qt's renderer opened the file an <image> named, beside the SVG,
// at any path, or on Windows \\server\share, where the system presents the user's credentials
// [code: qtsvg v6.12.0 qsvghandler.cpp:1967-1981]; resvg's default resolver does the same
// [code: usvg v0.48.1 parser/image.rs:85-103], which our shim replaces (D-90): two layers.
// Style sheets, scripts and event attributes stay: resvg follows no @import and runs nothing
// [code: usvg v0.48.1 renders static SVG only; simplecss 0.2.2 skips every at-rule] (D-82).
// Only the file itself may be an svgz: an SVG inside it inflated as well would multiply the work
// at each level, and browsers do not inflate a data: URL either. *hasText: an element named
// "text" (with its fonts, the renderer's slowest preparation) is somewhere in the document.
// Empty when the document cannot be read or is too large.
QByteArray safeSvg(QByteArrayView svg, int nesting, bool *hasText)
{
    const QByteArray xml = nesting == 0 && svg.startsWith("\x1f\x8b") ? gunzip(svg) // svgz
                           : svg.size() <= kMaxSvgBytes ? QByteArray::fromRawData(svg.data(), svg.size()) // read in place
                                                        : QByteArray();
    if (xml.isEmpty())
        return {};
    QXmlStreamReader reader(xml);
    reader.setNamespaceProcessing(false); // names as written
    QByteArray out;
    QXmlStreamWriter writer(&out);
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartDocument: writer.writeStartDocument(); break;
        case QXmlStreamReader::EndDocument: writer.writeEndDocument(); break;
        case QXmlStreamReader::StartElement: {
            const QStringView name = reader.qualifiedName();
            const QStringView local = name.sliced(name.lastIndexOf(QLatin1Char(':')) + 1);
            const bool image = local == QLatin1String("image");
            if (local == QLatin1String("text"))
                *hasText = true;
            writer.writeStartElement(name.toString());
            const QXmlStreamAttributes attributes = reader.attributes();
            for (const QXmlStreamAttribute &attribute : attributes) {
                const QStringView key = attribute.qualifiedName();
                QString value = attribute.value().toString();
                if (key == QLatin1String("href") || key.endsWith(QLatin1String(":href"))) {
                    const QStringView reference = QStringView(value).trimmed();
                    if (image)
                        value = reference.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)
                                    ? safeDataUrl(reference, nesting, hasText)
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

} // namespace

bool svgAvailable()
{
#ifdef IMAGEVIEWER_HAVE_SVG
    return iv_svg_contract_version() == IV_SVG_CONTRACT_VERSION;
#else
    return false;
#endif
}

QString svgRenderer()
{
#ifdef IMAGEVIEWER_HAVE_SVG
    return svgAvailable() ? QString::fromUtf8(iv_svg_renderer()) : QString();
#else
    return {};
#endif
}

#ifdef IMAGEVIEWER_HAVE_SVG
namespace {

// The document the worker read last, kept for the next request without a file (D-95): the zoom's
// details of one SVG come one after another, and each would read, make safe and parse it again.
struct HeldDocument {
    std::unique_ptr<iv_svg, void (*)(iv_svg *)> svg{nullptr, iv_svg_free};
    double width = 0.0, height = 0.0;
    qint64 xmlBytes = 0;
};
HeldDocument &heldDocument()
{
    static HeldDocument held;
    return held;
}

} // namespace
#endif

int runSvgWorker(QByteArrayView file, qint64 maxPixels, DecodeOptions options)
{
#ifdef IMAGEVIEWER_HAVE_SVG
    if (!svgAvailable()) {
        std::fprintf(stderr, "the SVG renderer implements another contract\n");
        return kWorkerFailed;
    }
    // The renderer's own memory: a document can declare a filter region many times its canvas, and
    // resvg allocates such a layer whole (a 3000 px SVG reached 2 GiB; D-93 draws layers only within
    // reach of the view, but a filter that needs its whole region still gets it). 16 times what it
    // reads, or draws, at least a quarter of this machine's memory (a deep zoom into large blurs
    // takes gigabytes, the owner, D-94), never more than its decode budget; past it the worker
    // ends, and the viewer says so (D-90).
    const auto limitMemory = [](qint64 bytes) {
        qint64 limit = std::max<qint64>(std::max(qint64(512) << 20, physicalMemoryBytes() / 4), 16 * bytes);
        if (const qint64 budget = decodeMemoryBudget(); budget > 0)
            limit = std::min(limit, budget);
        iv_svg_limit_memory(uint64_t(limit));
    };
    // Its bands at once on every core but two, left to the window, the GPU's driver and the system
    // (the owner, D-93).
    iv_svg_limit_threads(uint32_t(std::max(1, QThread::idealThreadCount() - 2)));
    HeldDocument &held = heldDocument();
    if (file.isEmpty()) { // the document held: the viewer sends none when it knows the worker has it
        if (!held.svg) {
            std::fprintf(stderr, "no document held\n");
            return kWorkerFailed;
        }
    } else {
        held = HeldDocument(); // the new document, or none
        bool hasText = false;
        const QByteArray xml = safeSvg(file, 0, &hasText);
        if (xml.isEmpty()) {
            std::fprintf(stderr, "not well-formed XML, or more than %lld MiB\n", static_cast<long long>(kMaxSvgBytes >> 20));
            return kWorkerFailed;
        }
        limitMemory(xml.size());
        iv_svg *parsed = nullptr;
        // The system's fonts only for a document with text: loading them is most of what a small
        // document costs (D-90 (3)).
        if (const int status = iv_svg_parse(reinterpret_cast<const uint8_t *>(xml.constData()), std::size_t(xml.size()),
                                            hasText ? 1 : 0, &parsed);
            status != IV_SVG_OK) {
            std::fprintf(stderr, "the renderer could not read the document (%d)\n", status);
            return kWorkerFailed;
        }
        HeldDocument document;
        document.svg.reset(parsed);
        document.xmlBytes = xml.size();
        if (iv_svg_size(parsed, &document.width, &document.height) != IV_SVG_OK || !(document.width > 0.0)
            || !(document.height > 0.0) || document.width > double(kMaxPixels) || document.height > double(kMaxPixels)) {
            std::fprintf(stderr, "invalid document size\n");
            return kWorkerFailed;
        }
        held = std::move(document);
    }
    const double documentWidth = held.width, documentHeight = held.height;
    // Its own size; within the side asked for, or (a thumbnail) exactly that side; or an area of it
    // at a scale (the zoom's detail, D-93).
    const double longest = std::max(documentWidth, documentHeight);
    const auto pixels = [](double side) { return std::max<qint64>(1, qint64(std::ceil(side - 1e-6))); };
    const bool area = options.scale > 0.0;
    double scale = options.maxSide > 0 && (options.fill || longest > options.maxSide) ? options.maxSide / longest : 1.0;
    if (!options.fill && options.view.width() > 0 && options.view.height() > 0) // no larger than shown
        scale = std::min({scale, options.view.width() / documentWidth, options.view.height() / documentHeight});
    if (area)
        scale = options.scale;
    const qint64 width = area ? qRound64(options.area.width()) : pixels(documentWidth * scale);
    const qint64 height = area ? qRound64(options.area.height()) : pixels(documentHeight * scale);
    if (width > kMaxPixels || height > kMaxPixels || width * height > maxPixels)
        return kWorkerOverLimit; // each side within 2^30 first: the product fits
    limitMemory(std::max<qint64>(width * height * 4, held.xmlBytes));
    std::unique_ptr<uint8_t[]> rgba(new (std::nothrow) uint8_t[std::size_t(width * height) * 4]);
    if (!rgba)
        return kWorkerOverLimit;
    if (const int status = iv_svg_render(held.svg.get(), scale, area ? options.area.left() : 0.0, area ? options.area.top() : 0.0,
                                         uint32_t(width), uint32_t(height), rgba.get());
        status != IV_SVG_OK) {
        std::fprintf(stderr, "the renderer failed (%d)\n", status);
        return kWorkerFailed;
    }
    WorkerAnswer answer;
    answer.width = quint32(width);
    answer.height = quint32(height);
    answer.channels = 4;
    answer.sampleBytes = 1;
    answer.bits = 8;
    answer.associatedAlpha = true; // the renderer's pixels as they are: image.cpp divides in float
    answer.documentWidth = quint32(pixels(documentWidth));
    answer.documentHeight = quint32(pixels(documentHeight));
    answer.documentScale = scale; // exact: the document's sides, rounded up above, rarely fill whole texels
    return writeWorkerAnswer(answer, nullptr, rgba.get()); // the next request follows (D-94)
#else
    Q_UNUSED(file);
    Q_UNUSED(maxPixels);
    Q_UNUSED(options);
    std::fprintf(stderr, "built without an SVG renderer\n");
    return kWorkerFailed;
#endif
}
