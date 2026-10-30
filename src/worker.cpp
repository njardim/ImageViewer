// The decode worker (decisions D-38, D-90): what must not run in the viewer's own process runs in
// a separate one, `ImageViewer --decode-worker <CODER> <max pixels>` for the long tail of formats
// (GraphicsMagick) and `ImageViewer --decode-worker SVG` for SVG (svg.cpp). The viewer sends the
// file's bytes on standard input; the worker answers with the pixels on standard output. A
// GraphicsMagick worker decodes one file and exits; the SVG worker of a decoding thread serves its
// requests one after another (kSvgRequest, D-94). GraphicsMagick keeps only the coders of the registry (no delegates,
// no external programs, no pseudo-formats) and has pixel, memory and read limits; every worker is
// killed when it takes too long. A crash there means "cannot open this file", never a crash of
// the viewer.
//
// Answer on standard output, all integers 32-bit little endian:
//   "IVWORKER" (8 bytes), width, height, channels (3 or 4), bytes per sample (1 or 2),
//   significant bits, EXIF orientation (1-8), ICC profile size, flags (bit 0: associated alpha),
//   the document's width and height (a vector image's own size; 0: width and height), then the
//   profile and the interleaved RGB(A) samples, rows from the top, native byte order.
// Exit codes (an SVG answer's reason codes too): kWorkerDecoded, kWorkerFailed (reason on standard
// error), kWorkerUsage, kWorkerOverLimit (decoders.h).
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX // std::min and std::max, not the macros of windows.h (GraphicsMagick includes it)
#endif

#include "decoders.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

#ifdef IMAGEVIEWER_HAVE_GRAPHICSMAGICK
// GraphicsMagick's C API calls its image "Image", like image.h: here it is GmImage. Only
// the C type name changes; the functions keep their C linkage.
#define Image GmImage
#include <magick/api.h>
#undef Image
#endif

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

Q_LOGGING_CATEGORY(lcWorker, "imageviewer.worker")

namespace {

constexpr char kMagic[8] = {'I', 'V', 'W', 'O', 'R', 'K', 'E', 'R'};
constexpr int kHeaderWords = 10;
// The SVG worker serves requests one after another on standard input (D-94): "IVSVGREQ", the pixel
// limit (64 bits), the side and fill of DecodeOptions (32 bits each), its scale and its area's left
// and top (IEEE doubles), the area's width and height, its view's width and height (32 bits each)
// and the file's size (64 bits), all little endian; then the file. A size of 0: the document of the
// worker's last request, kept parsed (D-95). Each answer is the decoded image
// (kMagic first) or "IVSVGNOT" and the reason's code (32 bits, the worker's exit codes), with the
// reason on standard error.
constexpr char kSvgRequest[8] = {'I', 'V', 'S', 'V', 'G', 'R', 'E', 'Q'};
constexpr std::size_t kSvgRequestBytes = sizeof kSvgRequest + 8 + 4 + 4 + 8 * 3 + 4 * 4 + 8;
constexpr char kSvgNotDecoded[8] = {'I', 'V', 'S', 'V', 'G', 'N', 'O', 'T'};
// Memory per pixel of a worker decode: GraphicsMagick's 16-bit RGBA cache and the answer in
// the worker, the answer and its copy here (fitsInMemory() adds the conversion's 16).
constexpr int kWorkerNativeBytesPerPixel = 8 + 8 + 8 + 8;
// An SVG: the renderer's pixmap and two of its layers (a group with a filter, a mask) in the
// worker, the answer and its copy here.
constexpr int kSvgNativeBytesPerPixel = 4 + 8 + 4 + 4;
constexpr qint64 kConversionBytesPerPixel = 16;
constexpr qint64 kMaxIccBytes = qint64(1) << 26;
constexpr int kTimeoutMs = 30000;

// The coders the worker keeps: the registry's GraphicsMagick formats (formats.cpp), by name.
// Everything else GraphicsMagick has (PostScript through Ghostscript, MSL, MVG, TXT, URL,
// the formats the application decodes itself) is unregistered before anything is read.
constexpr std::array<const char *, 16> kCoders = {"CUT",  "DCM", "DCX", "MAC", "MAT",   "MIFF", "OTB", "PCX",
                                                  "PICT", "PIX", "SUN", "TIM", "VICAR", "VIFF", "WPG", "XCF"};

bool isAllowedCoder(const char *name)
{
    for (const char *coder : kCoders)
        if (std::strcmp(coder, name) == 0)
            return true;
    return false;
}

constexpr qsizetype kHeaderBytes = qsizetype(sizeof kMagic) + kHeaderWords * 4;

// The answer's header, checked before anything else of it is used.
struct Answer : WorkerAnswer {
    qint64 dataBytes = 0;
    qint64 totalBytes() const { return kHeaderBytes + qint64(iccBytes) + dataBytes; }
};

bool parseHeader(const QByteArray &answer, qint64 limit, Answer *a)
{
    if (answer.size() < kHeaderBytes || std::memcmp(answer.constData(), kMagic, sizeof kMagic) != 0)
        return false;
    const auto word = [&answer](qsizetype at) { return qFromLittleEndian<quint32>(answer.constData() + at); };
    a->width = word(8);
    a->height = word(12);
    a->channels = word(16);
    a->sampleBytes = word(20);
    a->bits = word(24);
    a->orientation = word(28);
    a->iccBytes = word(32);
    const quint32 flags = word(36);
    a->associatedAlpha = (flags & 1) != 0;
    a->documentWidth = word(40);
    a->documentHeight = word(44);
    // The answer is untrusted (a worker may be compromised): every field is bounded before any
    // arithmetic, which the sizes of two 32-bit words could otherwise overflow.
    if (a->width == 0 || a->height == 0 || a->width > kMaxPixels || a->height > kMaxPixels
        || (a->channels != 3 && a->channels != 4) || (a->sampleBytes != 1 && a->sampleBytes != 2) || flags > 1
        || a->documentWidth > kMaxPixels || a->documentHeight > kMaxPixels
        || (a->documentWidth == 0) != (a->documentHeight == 0))
        return false;
    // Each side at most 2^30: their product fits, but not times 8 (2^30 x 2^30 x 4 x 2 overflowed).
    const qint64 pixels = qint64(a->width) * a->height;
    if (pixels > limit || pixels > kMaxPixels)
        return false;
    a->dataBytes = pixels * a->channels * a->sampleBytes;
    return a->bits > 0 && a->bits <= 16 && a->iccBytes <= kMaxIccBytes;
}

void writeWord(std::FILE *out, quint32 v)
{
    unsigned char bytes[4];
    qToLittleEndian(v, bytes);
    std::fwrite(bytes, 1, 4, out);
}

#ifdef IMAGEVIEWER_HAVE_GRAPHICSMAGICK
int fail(const char *what, const ExceptionInfo &exception)
{
    std::fprintf(stderr, "%s: %s%s%s\n", what, exception.reason ? exception.reason : "unknown error",
                 exception.description ? " " : "", exception.description ? exception.description : "");
    return kWorkerFailed;
}

// Over the limits this decode was given (its pixels, the memory they need), rather than a
// damaged file: GraphicsMagick raises a resource limit error as well for a width or height of 0
// or past WidthResource and HeightResource, as damaged headers have (CheckImagePixelLimits).
bool overLimit(const ExceptionInfo &exception)
{
    if (exception.severity != ResourceLimitError || !exception.reason)
        return false;
    for (const char *tag : {"Resource/Limit/Error/ImagePixelLimitExceeded", "Resource/Limit/Error/MemoryAllocationFailed"})
        if (const char *message = GetLocaleMessage(tag); message && std::strcmp(exception.reason, message) == 0)
            return true;
    return false;
}

// Unregisters every coder that is not in the allow-list, so that no file, and no image
// embedded in one (WPG, PICT), reaches another coder or an external program. Null when that
// worked; otherwise why the worker must not decode.
const char *keepOnlyAllowedCoders()
{
    ExceptionInfo exception;
    GetExceptionInfo(&exception);
    MagickInfo **all = GetMagickInfoArray(&exception);
    DestroyExceptionInfo(&exception);
    if (!all)
        return "no coder list";
    std::vector<std::string> unwanted;
    int allowed = 0;
    for (MagickInfo **info = all; *info; ++info) {
        if ((*info)->name && !isAllowedCoder((*info)->name))
            unwanted.emplace_back((*info)->name);
        else if ((*info)->name)
            ++allowed;
    }
    MagickFree(all);
    // A build that registers its coders only as loadable modules has none here.
    if (allowed != int(kCoders.size()))
        return "the allowed coders are not all built in";
    for (const std::string &name : unwanted)
        UnregisterMagickInfo(name.c_str());
    // No delegates (external programs) either: an allowed coder can hand an embedded image
    // to a missing one (DICOM's JPEG fragments), which GraphicsMagick then offers to a
    // delegate of delegates.mgk. The worker runs where no such file can be found (see
    // decodeInWorker()); if one is found anyway, decode nothing.
    // (Without a file, GraphicsMagick's list holds one empty placeholder, no command.)
    GetExceptionInfo(&exception);
    bool delegates = false;
    for (const DelegateInfo *delegate = GetDelegateInfo("*", "*", &exception); delegate; delegate = delegate->next)
        delegates = delegates || (delegate->commands && *delegate->commands);
    DestroyExceptionInfo(&exception);
    if (delegates)
        return "a delegate (external program) is configured";
    // A build with loadable coder modules would load an unregistered coder again when asked
    // for it: check that the dangerous ones are really gone, or decode nothing.
    for (const char *name : {"PS", "EPS", "PDF", "MSL", "MVG", "TXT", "URL", "HTTP", "SVG", "MPC"}) {
        GetExceptionInfo(&exception);
        const bool present = GetMagickInfo(name, &exception) != nullptr;
        DestroyExceptionInfo(&exception);
        if (present)
            return "a removed coder can still be loaded";
    }
    return nullptr;
}
#endif

} // namespace

int writeWorkerAnswer(const WorkerAnswer &answer, const unsigned char *icc, const unsigned char *pixels)
{
    std::fwrite(kMagic, 1, sizeof kMagic, stdout);
    for (quint32 word : {answer.width, answer.height, answer.channels, answer.sampleBytes, answer.bits,
                         answer.orientation, answer.iccBytes, quint32(answer.associatedAlpha ? 1 : 0),
                         answer.documentWidth, answer.documentHeight})
        writeWord(stdout, word);
    if (answer.iccBytes)
        std::fwrite(icc, 1, answer.iccBytes, stdout);
    std::fwrite(pixels, 1, std::size_t(answer.width) * answer.height * answer.channels * answer.sampleBytes, stdout);
    return std::fflush(stdout) == 0 && !std::ferror(stdout) ? kWorkerDecoded : kWorkerFailed;
}

bool graphicsMagickAvailable()
{
#ifdef IMAGEVIEWER_HAVE_GRAPHICSMAGICK
    return true;
#else
    return false;
#endif
}

namespace {

// The worker's SVG mode (D-94): one request after another, until the viewer closes the pipe (then
// exit 0) or a request breaks the protocol (exit 2). A document that is not decoded gets its
// reason as an answer, and the next request is served.
int serveSvgRequests()
{
    for (;;) {
        std::array<unsigned char, kSvgRequestBytes> head;
        const std::size_t got = std::fread(head.data(), 1, head.size(), stdin);
        if (got == 0 && std::feof(stdin))
            return kWorkerDecoded;
        if (got != head.size() || std::memcmp(head.data(), kSvgRequest, sizeof kSvgRequest) != 0) {
            std::fprintf(stderr, "no SVG request\n");
            return kWorkerUsage;
        }
        const unsigned char *h = head.data();
        const quint64 limit = qFromLittleEndian<quint64>(h + 8);
        const quint32 side = qFromLittleEndian<quint32>(h + 16), fill = qFromLittleEndian<quint32>(h + 20);
        const auto real = [h](std::size_t at) { return std::bit_cast<double>(qFromLittleEndian<quint64>(h + at)); };
        const double scale = real(24), left = real(32), top = real(40);
        const quint32 width = qFromLittleEndian<quint32>(h + 48), height = qFromLittleEndian<quint32>(h + 52);
        const quint32 viewWidth = qFromLittleEndian<quint32>(h + 56), viewHeight = qFromLittleEndian<quint32>(h + 60);
        const quint64 fileBytes = qFromLittleEndian<quint64>(h + 64);
        const bool area = scale > 0.0;
        if (limit == 0 || limit > quint64(kMaxPixels) || side > quint32(kMaxPixels) || fill > 1
            || !(scale >= 0.0 && scale <= double(kMaxPixels)) || !(std::abs(left) <= double(kMaxPixels))
            || !(std::abs(top) <= double(kMaxPixels))
            || (area && (width == 0 || height == 0 || width > quint32(kMaxPixels) || height > quint32(kMaxPixels)))
            || viewWidth > quint32(kMaxPixels) || viewHeight > quint32(kMaxPixels) || fileBytes > quint64(kMaxFileBytes)) {
            std::fprintf(stderr, "invalid SVG request\n");
            return kWorkerUsage;
        }
        std::vector<unsigned char> file;
        try {
            file.resize(std::size_t(fileBytes));
        } catch (const std::bad_alloc &) {
            std::fprintf(stderr, "memory allocation of %llu bytes failed\n", static_cast<unsigned long long>(fileBytes));
            return kWorkerFailed;
        }
        if (std::fread(file.data(), 1, file.size(), stdin) != file.size()) {
            std::fprintf(stderr, "the file ended early\n");
            return kWorkerUsage;
        }
        DecodeOptions options;
        options.maxSide = int(side);
        options.fill = fill == 1;
        options.view = QSize(int(viewWidth), int(viewHeight));
        if (area) {
            options.scale = scale;
            options.area = QRectF(left, top, width, height);
        }
        // The answer, written by runSvgWorker() when decoded; otherwise its reason here.
        if (const int code = runSvgWorker(QByteArrayView(reinterpret_cast<const char *>(file.data()), qsizetype(file.size())),
                                          qint64(limit), options);
            code != kWorkerDecoded) {
            std::fwrite(kSvgNotDecoded, 1, sizeof kSvgNotDecoded, stdout);
            writeWord(stdout, quint32(code));
        }
        std::fflush(stderr);
        if (std::fflush(stdout) != 0 || std::ferror(stdout))
            return kWorkerFailed;
    }
}

} // namespace

int runDecodeWorker(int argc, char **argv)
{
#if defined(Q_OS_WIN)
    // No "stopped working" dialog for a crash here, and binary pipes.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#elif defined(Q_OS_UNIX)
    const rlimit noCore{0, 0};
    setrlimit(RLIMIT_CORE, &noCore);
#endif
    const bool svg = argc == 3 && std::strcmp(argv[2], "SVG") == 0;
    if (!svg && (argc != 4 || !isAllowedCoder(argv[2]))) {
        std::fprintf(stderr, "usage: ImageViewer --decode-worker <coder> <max pixels>\n"
                             "       ImageViewer --decode-worker SVG (requests on standard input)\n");
        return kWorkerUsage;
    }
    if (svg)
        return serveSvgRequests();
    const char *coder = argv[2];
    const long long maxPixels = std::atoll(argv[3]);
    if (maxPixels <= 0 || maxPixels > kMaxPixels) {
        std::fprintf(stderr, "invalid pixel limit\n");
        return kWorkerUsage;
    }
    std::vector<unsigned char> input;
    std::array<unsigned char, 65536> chunk;
    for (std::size_t n; (n = std::fread(chunk.data(), 1, chunk.size(), stdin)) > 0;) {
        if (qint64(input.size() + n) > kMaxFileBytes) {
            std::fprintf(stderr, "file too large\n");
            return kWorkerFailed;
        }
        input.insert(input.end(), chunk.begin(), chunk.begin() + std::ptrdiff_t(n));
    }
    if (input.empty()) {
        std::fprintf(stderr, "empty file\n");
        return kWorkerFailed;
    }
#ifdef IMAGEVIEWER_HAVE_GRAPHICSMAGICK
    // GraphicsMagick looks for its configuration files (delegates.mgk, log.mgk) around the
    // directory of the "client" it is given: an empty file in the private working directory
    // the viewer made for this decode (decodeInWorker()), so it finds none.
    const std::string client = (std::filesystem::current_path() / "bin" / "ImageViewer").string();
    InitializeMagick(std::filesystem::exists(client) ? client.c_str() : argv[0]);
    if (const char *why = keepOnlyAllowedCoders()) {
        std::fprintf(stderr, "cannot restrict the coders and delegates: %s\n", why);
        return kWorkerFailed;
    }
    // Pixels as asked, 16-bit RGBA in memory at most, nothing on disk.
    SetMagickResourceLimit(PixelsResource, maxPixels);
    SetMagickResourceLimit(WidthResource, 1 << 20);
    SetMagickResourceLimit(HeightResource, 1 << 20);
    SetMagickResourceLimit(MemoryResource, std::min<magick_int64_t>(magick_int64_t(maxPixels) * 16, magick_int64_t(8) << 30));
    SetMagickResourceLimit(MapResource, 0);
    SetMagickResourceLimit(DiskResource, 0);
    SetMagickResourceLimit(ReadResource, magick_int64_t(4) << 30);
    SetMagickResourceLimit(ImagesResource, 4096); // XCF: one per layer
    SetMagickResourceLimit(ThreadsResource, 1);

    ExceptionInfo exception;
    GetExceptionInfo(&exception);
    ImageInfo *info = CloneImageInfo(nullptr);
    // "CODER:" fixes the coder: GraphicsMagick does not guess another one from the content.
    std::snprintf(info->filename, MaxTextExtent, "%s:image", coder);
    std::snprintf(info->magick, MaxTextExtent, "%s", coder);
    // The first image of a multi-image file; but XCF composites the layers it reads, so all.
    info->subimage = 0;
    info->subrange = std::strcmp(coder, "XCF") == 0 ? 0 : 1;
    GmImage *image = BlobToImage(info, input.data(), input.size(), &exception);
    DestroyImageInfo(info);
    if (!image) {
        const int code = overLimit(exception) ? kWorkerOverLimit : fail("not decoded", exception);
        DestroyExceptionInfo(&exception);
        return code;
    }
    // GraphicsMagick's XCF reader returns the bottom layer on the canvas and the others as
    // the next images (its own compositing is disabled): composite them, each with its blend
    // mode and offset; hidden layers carry NoCompositeOp. Not FlattenImages(), which would
    // put the background colour under a transparent canvas. Any other coder's next images are
    // pages or frames (each reader stops after the first, subrange 1): never composited.
    if (std::strcmp(coder, "XCF") == 0)
        for (GmImage *layer = image->next; layer; layer = layer->next)
            (void) CompositeImage(image, layer->compose, layer, layer->page.x, layer->page.y);
    if (!IsRGBCompatibleColorspace(image->colorspace) && !TransformColorspace(image, RGBColorspace)) {
        const int code = fail("colour space", image->exception);
        DestroyImageList(image);
        return code;
    }
    const unsigned long width = image->columns, height = image->rows;
    const bool alpha = image->matte != MagickFalse;
    const bool wide = image->depth > 8;
    const int channels = alpha ? 4 : 3;
    const std::size_t sampleBytes = wide ? 2 : 1;
    if (width == 0 || height == 0 || qint64(width) * qint64(height) > maxPixels) {
        DestroyImageList(image);
        return kWorkerOverLimit;
    }
    std::vector<unsigned char> pixels(std::size_t(width) * height * std::size_t(channels) * sampleBytes);
    if (!DispatchImage(image, 0, 0, width, height, alpha ? "RGBA" : "RGB", wide ? ShortPixel : CharPixel, pixels.data(),
                       &exception)) {
        const int code = fail("pixels", exception);
        DestroyImageList(image);
        DestroyExceptionInfo(&exception);
        return code;
    }
    std::size_t iccBytes = 0;
    const unsigned char *icc = GetImageProfile(image, "ICM", &iccBytes);
    if (!icc || qint64(iccBytes) > kMaxIccBytes)
        iccBytes = 0;
    WorkerAnswer answer;
    answer.width = quint32(width);
    answer.height = quint32(height);
    answer.channels = quint32(channels);
    answer.sampleBytes = quint32(sampleBytes);
    answer.bits = std::min<unsigned>(image->depth, 16);
    answer.orientation = int(image->orientation) >= 1 && int(image->orientation) <= 8 ? quint32(image->orientation) : 1;
    answer.iccBytes = quint32(iccBytes);
    const int code = writeWorkerAnswer(answer, icc, pixels.data());
    DestroyImageList(image);
    DestroyExceptionInfo(&exception);
    return code;
#else
    Q_UNUSED(coder);
    std::fprintf(stderr, "built without GraphicsMagick\n");
    return kWorkerFailed;
#endif
}

namespace {

// The SVG worker of each decoding thread (D-94): started on the thread's first SVG, it serves the
// thread's next ones (its libraries and the system's fonts load once), and ends with the thread:
// the decode pools' threads exit with the window, or after QThreadPool's idle timeout (30 s), so no
// worker waits while no SVG is read. A request that fails it (a crash, its memory limit, 30 s, a
// broken answer, a cancel) ends it, and the next SVG starts another. Kept only in the graphical
// viewer's decoding threads: a console mode decodes one file and exits.
struct StopWorker {
    void operator()(QProcess *worker) const
    {
        worker->closeWriteChannel(); // an idle SVG worker exits at once
        if (!worker->waitForFinished(200)) {
            worker->kill();
            worker->waitForFinished(1000);
        }
        delete worker;
    }
};
using WorkerProcess = std::unique_ptr<QProcess, StopWorker>;
thread_local WorkerProcess t_svgWorker;
thread_local QElapsedTimer t_svgWorkerIdle; // since its last answer (stopIdleSvgWorker())
// The SHA-256 of the document the thread's SVG worker holds parsed, once it has answered with its
// image: the next request for the same bytes, a zoom's detail, sends none (D-95).
thread_local QByteArray t_svgWorkerDocument;
constexpr qint64 kSvgWorkerIdleMs = 30000;

// Windows shows its busy cursor for two seconds after it starts a program with a window, until that
// program shows one: meant for the programs the user starts, not for a worker, which has none.
void withoutLaunchFeedback(QProcess *worker)
{
#ifdef Q_OS_WIN
    worker->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->startupInfo->dwFlags |= STARTF_FORCEOFFFEEDBACK;
    });
#else
    Q_UNUSED(worker);
#endif
}

WorkerProcess takeSvgWorker()
{
    if (t_svgWorker && t_svgWorker->state() != QProcess::NotRunning)
        return std::move(t_svgWorker);
    WorkerProcess worker(new QProcess);
    withoutLaunchFeedback(worker.get());
    worker->setProgram(QCoreApplication::applicationFilePath());
    worker->setArguments({QStringLiteral("--decode-worker"), QStringLiteral("SVG")});
    worker->start();
    t_svgWorkerDocument.clear();
    qCInfo(lcWorker) << "SVG worker started";
    return worker;
}

void keepSvgWorker(WorkerProcess worker)
{
    if (QCoreApplication *app = QCoreApplication::instance();
        app && app->inherits("QGuiApplication") && QThread::currentThread() != app->thread()) {
        t_svgWorker = std::move(worker);
        t_svgWorkerIdle.start();
    }
}

bool runWorker(const Format &format, const QString &path, qint64 maxPixels, Decoded *out, QString *error,
               DecodeOptions options)
{
    const bool svg = format.decoder == Decoder::Svg;
    QByteArray bytes;
    if (!readWholeFile(path, &bytes, error))
        return false;
    if (bytes.isEmpty()) {
        *error = damaged(format.name);
        return false;
    }
    // The pixels asked for, within what this machine's memory can take (checked again with
    // the real size as soon as the answer's header arrives).
    const int nativeBytesPerPixel = svg ? kSvgNativeBytesPerPixel : kWorkerNativeBytesPerPixel;
    const qint64 budget = decodeMemoryBudget();
    qint64 limit = maxPixels > 0 ? std::min(maxPixels, kMaxPixels) : kMaxPixels;
    if (budget > 0)
        limit = std::max<qint64>(1, std::min(limit, budget / (nativeBytesPerPixel + kConversionBytesPerPixel)));
    WorkerProcess process;
    std::optional<QTemporaryDir> sandbox; // GraphicsMagick only
    QByteArray request, document;
    if (svg) {
        // The SVG renderer reads nothing but the document and, for its text, the system's fonts
        // (the user's own ones and fontconfig's settings through HOME): the environment as it is.
        process = takeSvgWorker();
        document = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        if (document == t_svgWorkerDocument) {
            bytes.clear(); // a size of 0: the one it holds
            qCInfo(lcWorker) << "SVG document held by its worker:" << QFileInfo(path).fileName();
        }
        t_svgWorkerDocument.clear(); // until this request's image arrives
        request.resize(qsizetype(kSvgRequestBytes));
        request.fill('\0');
        std::memcpy(request.data(), kSvgRequest, sizeof kSvgRequest);
        qToLittleEndian(quint64(limit), request.data() + 8);
        qToLittleEndian(quint32(std::max(options.maxSide, 0)), request.data() + 16);
        qToLittleEndian(quint32(options.fill ? 1 : 0), request.data() + 20);
        const bool area = options.scale > 0.0 && !options.area.isEmpty();
        qToLittleEndian(std::bit_cast<quint64>(area ? options.scale : 0.0), request.data() + 24);
        qToLittleEndian(std::bit_cast<quint64>(area ? options.area.left() : 0.0), request.data() + 32);
        qToLittleEndian(std::bit_cast<quint64>(area ? options.area.top() : 0.0), request.data() + 40);
        qToLittleEndian(quint32(area ? qRound(options.area.width()) : 0), request.data() + 48);
        qToLittleEndian(quint32(area ? qRound(options.area.height()) : 0), request.data() + 52);
        qToLittleEndian(quint32(std::clamp(options.view.width(), 0, int(kMaxPixels))), request.data() + 56);
        qToLittleEndian(quint32(std::clamp(options.view.height(), 0, int(kMaxPixels))), request.data() + 60);
        qToLittleEndian(quint64(bytes.size()), request.data() + 64);
    } else {
        process.reset(new QProcess);
        QProcess &worker = *process;
        // The worker sees no GraphicsMagick settings of the user's environment (configuration
        // paths, debug logging, coder stability), only the arguments.
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const QString &key : environment.keys())
            if (key.startsWith(QLatin1String("MAGICK_"), Qt::CaseInsensitive))
                environment.remove(key);
        // A private, empty working directory (QTemporaryDir: owner only), removed afterwards with
        // whatever the worker left there:
        // - coders that look for companion files by a relative name (CUT's palette) find nothing;
        // - GraphicsMagick's temporary files (MAT and DICOM copy data to disk) land here and
        //   go, even when the worker crashes or is stopped;
        // - "bin/ImageViewer", an empty file, is the client path GraphicsMagick searches for
        //   its configuration (delegates.mgk names external programs), as is HOME (~/.magick).
        sandbox.emplace();
        QFile client(sandbox->path() + QStringLiteral("/bin/ImageViewer"));
        if (!sandbox->isValid() || !QDir(sandbox->path()).mkdir(QStringLiteral("bin")) || !client.open(QIODevice::WriteOnly)) {
            *error = damaged(format.name);
            return false;
        }
        client.close();
        worker.setWorkingDirectory(sandbox->path());
        const QString place = QDir::toNativeSeparators(sandbox->path());
        for (const char *variable : {"MAGICK_TMPDIR", "TMPDIR", "TMP", "TEMP", "HOME", "MAGICK_CODER_MODULE_PATH"})
            environment.insert(QString::fromLatin1(variable), place);
        worker.setProcessEnvironment(environment);
        worker.setArguments({QStringLiteral("--decode-worker"), QString::fromLatin1(format.id).toUpper(), QString::number(limit)});
        worker.setProgram(QCoreApplication::applicationFilePath());
        withoutLaunchFeedback(&worker);
        worker.start();
    }
    QProcess &worker = *process;
    if (!worker.waitForStarted(kTimeoutMs)) {
        qCWarning(lcWorker) << "decode worker did not start:" << worker.errorString();
        *error = damaged(format.name);
        return false;
    }
    worker.readAllStandardError(); // what a reused SVG worker said for an earlier request
    if (!request.isEmpty())
        worker.write(request);
    worker.write(bytes);
    if (!svg) // the SVG worker stays for the next request
        worker.closeWriteChannel();
    bytes.clear();

    // The header comes first and says how much follows: anything else, or more, means a broken
    // worker, which is stopped. The pixels are read straight into the decoded image's buffer.
    QByteArray head; // the header, then the ICC profile
    std::unique_ptr<unsigned char[]> data;
    qint64 got = 0;
    QByteArray messages;
    Answer a;
    QString tooLarge;
    bool haveHeader = false, broken = false, timedOut = false, cancelled = false;
    // An SVG answer is complete without the worker exiting: the image, or its reason's code.
    std::optional<quint32> notDecoded;
    const auto complete = [&] {
        return svg && (notDecoded || (haveHeader && head.size() == kHeaderBytes + qint64(a.iccBytes) && got == a.dataBytes));
    };
    QElapsedTimer clock;
    clock.start();
    const auto collect = [&] {
        const QByteArray said = worker.readAllStandardError(); // always drained: QProcess would keep it all
        if (messages.size() < 4096)
            messages += said.left(4096 - messages.size());
        while (worker.bytesAvailable() > 0 && !broken && !complete()) {
            if (!haveHeader) {
                head += worker.read(kHeaderBytes - head.size());
                if (svg && head.size() >= qsizetype(sizeof kSvgNotDecoded)
                    && std::memcmp(head.constData(), kSvgNotDecoded, sizeof kSvgNotDecoded) == 0) {
                    const qsizetype record = qsizetype(sizeof kSvgNotDecoded) + 4;
                    if (head.size() > record)
                        broken = true; // nothing follows a reason until the next request
                    else if (head.size() == record)
                        notDecoded = qFromLittleEndian<quint32>(head.constData() + sizeof kSvgNotDecoded);
                    continue;
                }
                if (head.size() < kHeaderBytes)
                    continue;
                haveHeader = parseHeader(head, limit, &a);
                // The real size is known: no use waiting for an image this machine cannot take.
                broken = !haveHeader || !fitsInMemory(qint64(a.width) * a.height, nativeBytesPerPixel, &tooLarge);
                if (!broken) {
                    data.reset(new (std::nothrow) unsigned char[std::size_t(a.dataBytes)]);
                    if (!data)
                        tooLarge = QCoreApplication::translate("Image", "Not enough memory to decode the image.");
                    broken = !data;
                }
            } else if (head.size() < kHeaderBytes + qint64(a.iccBytes)) {
                head += worker.read(kHeaderBytes + qint64(a.iccBytes) - head.size());
            } else if (got < a.dataBytes) {
                const qint64 n = worker.read(reinterpret_cast<char *>(data.get()) + got, a.dataBytes - got);
                broken = n < 0;
                got += std::max<qint64>(n, 0);
            } else {
                broken = true; // more than the header announced
            }
        }
    };
    // A decode that may be cancelled looks at its flag every 20 ms (D-93).
    const qint64 step = options.cancel ? 20 : 1000;
    while (worker.state() != QProcess::NotRunning && !broken && !complete()) {
        if (options.cancel && options.cancel->load(std::memory_order_relaxed)) {
            cancelled = true;
            break;
        }
        const qint64 left = kTimeoutMs - clock.elapsed();
        if (left <= 0) {
            timedOut = true;
            break;
        }
        worker.waitForReadyRead(int(std::min(left, step)));
        collect();
    }
    if (complete()) {
        if (!notDecoded)
            t_svgWorkerDocument = document;
        keepSvgWorker(std::move(process)); // healthy: it serves the thread's next SVG
    } else if (worker.state() != QProcess::NotRunning) {
        worker.kill();
        worker.waitForFinished(5000);
    }
    if (cancelled) {
        *error = QStringLiteral("cancelled"); // never shown: the caller asked for it
        return false;
    }
    if (!broken && !complete())
        collect();
    if (timedOut) {
        qCWarning(lcWorker) << "decode worker stopped after" << kTimeoutMs << "ms:" << path;
        //: %1: seconds, e.g. "30".
        *error = QCoreApplication::translate("Image", "the decoder took longer than %1 s and was stopped")
                     .arg(QLocale().toString(kTimeoutMs / 1000));
        return false;
    }
    if (!tooLarge.isEmpty()) {
        *error = tooLarge;
        return false;
    }
    // The SVG renderer past its memory limit (svg.cpp): Rust's own message, then the worker ends.
    if (svg && !complete() && messages.contains("memory allocation of")) {
        qCWarning(lcWorker) << "decode worker out of its memory limit:" << path;
        *error = QCoreApplication::translate("Image", "Not enough memory to decode the image.");
        return false;
    }
    if (broken || (svg ? !complete() : worker.exitStatus() != QProcess::NormalExit)) {
        qCWarning(lcWorker) << "decode worker failed:" << worker.exitStatus() << worker.exitCode() << "broken answer" << broken
                            << path;
        *error = damaged(format.name);
        return false;
    }
    const int code = svg ? (notDecoded ? int(std::min<quint32>(*notDecoded, 255)) : kWorkerDecoded) : worker.exitCode();
    if (code == kWorkerOverLimit) {
        // Over the pixels asked for (preloading skips it), or over this machine's memory.
        if (maxPixels > 0)
            out->overLimit = true;
        else
            *error = QCoreApplication::translate("Image", "Not enough memory to decode the image.");
        return false;
    }
    if (code != kWorkerDecoded || !haveHeader || head.size() != kHeaderBytes + qint64(a.iccBytes) || got != a.dataBytes) {
        qCInfo(lcWorker) << format.name << "not decoded:" << code << messages.trimmed();
        *error = damaged(format.name);
        return false;
    }
    out->width = int(a.width);
    out->height = int(a.height);
    out->sample = a.sampleBytes == 2 ? Decoded::Sample::U16 : Decoded::Sample::U8;
    out->channels = int(a.channels);
    out->sourceChannels = int(a.channels);
    out->alphaIndex = a.channels == 4 ? 3 : -1;
    out->associatedAlpha = a.associatedAlpha && out->alphaIndex >= 0;
    out->bits = int(a.bits);
    out->orientation = a.orientation >= 1 && a.orientation <= 8 ? int(a.orientation) : 1;
    out->vector = svg;
    out->documentWidth = int(a.documentWidth);
    out->documentHeight = int(a.documentHeight);
    out->codec = svg ? svgRenderer() + QStringLiteral(" (worker)")
                     : QStringLiteral("GraphicsMagick %1 (worker)").arg(QString::fromLatin1(format.id).toUpper());
    if (a.iccBytes > 0)
        describeIcc(head.sliced(kHeaderBytes), &out->colour);
    else
        assumeDefault(false, &out->colour); // SVG: its colours are sRGB
    out->data = std::move(data);
    return true;
}

} // namespace

bool decodeInWorker(const Format &format, const QString &path, qint64 maxPixels, Decoded *out, QString *error,
                    DecodeOptions options)
{
    return runWorker(format, path, maxPixels, out, error, options);
}

void stopIdleSvgWorker()
{
    if (t_svgWorker && t_svgWorkerIdle.isValid() && t_svgWorkerIdle.hasExpired(kSvgWorkerIdleMs))
        t_svgWorker.reset();
}

QString graphicsMagickVersion()
{
#ifdef IMAGEVIEWER_HAVE_GRAPHICSMAGICK
    return QStringLiteral(MagickLibVersionText);
#else
    return {};
#endif
}
