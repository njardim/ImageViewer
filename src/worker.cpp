// The decode worker (decisions D-38, D-90): what must not run in the viewer's own process runs in
// a separate one, `ImageViewer --decode-worker <CODER> <max pixels>` for the long tail of formats
// (GraphicsMagick) and `ImageViewer --decode-worker SVG` for SVG (svg.cpp). The viewer sends the
// file's bytes on standard input, for SVG after a request (kSvgRequest, the limits: so that a
// worker started ahead of its decode serves any SVG); the worker answers with the pixels on
// standard output. GraphicsMagick keeps only the coders of the registry (no delegates,
// no external programs, no pseudo-formats) and has pixel, memory and read limits; every worker is
// killed when it takes too long. A crash there means "cannot open this file", never a crash of
// the viewer.
//
// Answer on standard output, all integers 32-bit little endian:
//   "IVWORKER" (8 bytes), width, height, channels (3 or 4), bytes per sample (1 or 2),
//   significant bits, EXIF orientation (1-8), ICC profile size, flags (bit 0: associated alpha),
//   the document's width and height (a vector image's own size; 0: width and height), then the
//   profile and the interleaved RGB(A) samples, rows from the top, native byte order.
// Exit codes: kWorkerDecoded, kWorkerFailed (reason on standard error), kWorkerUsage,
// kWorkerOverLimit (decoders.h).
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX // std::min and std::max, not the macros of windows.h (GraphicsMagick includes it)
#endif

#include "decoders.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocale>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtEndian>

#include <algorithm>
#include <array>
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
// An SVG request on standard input, before the file: "IVSVGREQ", the pixel limit (64 bits), the
// side and fill of VectorSize (32 bits each), all little endian.
constexpr char kSvgRequest[8] = {'I', 'V', 'S', 'V', 'G', 'R', 'E', 'Q'};
constexpr std::size_t kSvgRequestBytes = sizeof kSvgRequest + 8 + 4 + 4;
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
                             "       ImageViewer --decode-worker SVG (the request on standard input)\n");
        return kWorkerUsage;
    }
    const char *coder = argv[2];
    const long long maxPixels = svg ? 0 : std::atoll(argv[3]);
    if (!svg && (maxPixels <= 0 || maxPixels > kMaxPixels)) {
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
    if (input.empty() && !svg) {
        std::fprintf(stderr, "empty file\n");
        return kWorkerFailed;
    }
    if (svg) {
        if (input.size() < kSvgRequestBytes || std::memcmp(input.data(), kSvgRequest, sizeof kSvgRequest) != 0) {
            std::fprintf(stderr, "no SVG request\n");
            return kWorkerUsage;
        }
        const quint64 limit = qFromLittleEndian<quint64>(input.data() + 8);
        const quint32 side = qFromLittleEndian<quint32>(input.data() + 16), fill = qFromLittleEndian<quint32>(input.data() + 20);
        if (limit == 0 || limit > quint64(kMaxPixels) || side > quint32(kMaxPixels) || fill > 1) {
            std::fprintf(stderr, "invalid SVG request\n");
            return kWorkerUsage;
        }
        input.erase(input.begin(), input.begin() + std::ptrdiff_t(kSvgRequestBytes));
        if (input.empty()) {
            std::fprintf(stderr, "empty file\n");
            return kWorkerFailed;
        }
        return runSvgWorker(input, qint64(limit), VectorSize{int(side), fill == 1});
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

// The SVG worker each decoding thread keeps started ahead of its next SVG (D-90): starting the
// executable costs more than drawing most documents (its libraries load), and a process that starts
// while the thread waits costs the decode nothing. Every document still has a process of its own:
// a spare serves one request, then the next is started. Only in the graphical viewer (a console mode
// decodes one file and exits). Destroyed with its thread: the decode pools' threads exit with the
// window, or after QThreadPool's idle timeout.
struct StopWorker {
    void operator()(QProcess *worker) const
    {
        worker->kill();
        worker->waitForFinished(1000);
        delete worker;
    }
};
using WorkerProcess = std::unique_ptr<QProcess, StopWorker>;
thread_local WorkerProcess t_spareSvgWorker;

WorkerProcess startSvgWorker()
{
    WorkerProcess worker(new QProcess);
    worker->setProgram(QCoreApplication::applicationFilePath());
    worker->setArguments({QStringLiteral("--decode-worker"), QStringLiteral("SVG")});
    worker->start();
    return worker;
}

WorkerProcess takeSvgWorker()
{
    WorkerProcess worker = std::move(t_spareSvgWorker);
    return worker && worker->state() != QProcess::NotRunning ? std::move(worker) : startSvgWorker();
}

void prepareSvgWorker()
{
    if (QCoreApplication *app = QCoreApplication::instance(); app && app->inherits("QGuiApplication"))
        t_spareSvgWorker = startSvgWorker();
}

bool runWorker(const Format &format, const QString &path, qint64 maxPixels, Decoded *out, QString *error,
               VectorSize vector)
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
    QByteArray request;
    if (svg) {
        // The SVG renderer reads nothing but the document and, for its text, the system's fonts
        // (the user's own ones and fontconfig's settings through HOME): the environment as it is.
        process = takeSvgWorker();
        request.resize(qsizetype(kSvgRequestBytes));
        std::memcpy(request.data(), kSvgRequest, sizeof kSvgRequest);
        qToLittleEndian(quint64(limit), request.data() + 8);
        qToLittleEndian(quint32(std::max(vector.maxSide, 0)), request.data() + 16);
        qToLittleEndian(quint32(vector.fill ? 1 : 0), request.data() + 20);
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
        worker.start();
    }
    QProcess &worker = *process;
    if (!worker.waitForStarted(kTimeoutMs)) {
        qCWarning(lcWorker) << "decode worker did not start:" << worker.errorString();
        *error = damaged(format.name);
        return false;
    }
    if (!request.isEmpty())
        worker.write(request);
    worker.write(bytes);
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
    bool haveHeader = false, broken = false, timedOut = false;
    QElapsedTimer clock;
    clock.start();
    const auto collect = [&] {
        const QByteArray said = worker.readAllStandardError(); // always drained: QProcess would keep it all
        if (messages.size() < 4096)
            messages += said.left(4096 - messages.size());
        while (worker.bytesAvailable() > 0 && !broken) {
            if (!haveHeader) {
                head += worker.read(kHeaderBytes - head.size());
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
    while (worker.state() != QProcess::NotRunning && !broken) {
        const qint64 left = kTimeoutMs - clock.elapsed();
        if (left <= 0) {
            timedOut = true;
            break;
        }
        worker.waitForReadyRead(int(std::min<qint64>(left, 1000)));
        collect();
    }
    if (worker.state() != QProcess::NotRunning) {
        worker.kill();
        worker.waitForFinished(5000);
    }
    if (!broken)
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
    if (broken || worker.exitStatus() != QProcess::NormalExit) {
        qCWarning(lcWorker) << "decode worker failed:" << worker.exitStatus() << worker.exitCode() << "broken answer" << broken
                            << path;
        *error = damaged(format.name);
        return false;
    }
    if (worker.exitCode() == kWorkerOverLimit) {
        // Over the pixels asked for (preloading skips it), or over this machine's memory.
        if (maxPixels > 0)
            out->overLimit = true;
        else
            *error = QCoreApplication::translate("Image", "Not enough memory to decode the image.");
        return false;
    }
    if (worker.exitCode() != kWorkerDecoded || !haveHeader || head.size() != kHeaderBytes + qint64(a.iccBytes)
        || got != a.dataBytes) {
        qCInfo(lcWorker) << format.name << "not decoded:" << worker.exitCode() << messages.trimmed();
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
                    VectorSize vector)
{
    if (format.decoder == Decoder::Svg) {
        const bool decoded = runWorker(format, path, maxPixels, out, error, vector);
        prepareSvgWorker(); // for the next SVG on this thread
        return decoded;
    }
    return runWorker(format, path, maxPixels, out, error, vector);
}

QString graphicsMagickVersion()
{
#ifdef IMAGEVIEWER_HAVE_GRAPHICSMAGICK
    return QStringLiteral(MagickLibVersionText);
#else
    return {};
#endif
}
