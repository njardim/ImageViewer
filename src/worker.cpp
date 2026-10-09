// The decode worker (decision D-38): the long tail of formats goes to GraphicsMagick, which
// runs only in a separate process, `ImageViewer --decode-worker <CODER> <max pixels>`. The
// viewer sends the file's bytes on standard input; the worker answers with the pixels on
// standard output. The worker keeps only the coders of the registry (no delegates, no
// external programs, no pseudo-formats), has pixel, memory and read limits, and is killed
// when it takes too long. A crash there means "cannot open this file", never a crash of
// the viewer.
//
// Answer on standard output, all integers 32-bit little endian:
//   "IVWORKER" (8 bytes), width, height, channels (3 or 4), bytes per sample (1 or 2),
//   significant bits, EXIF orientation (1-8), ICC profile size, then the profile and the
//   interleaved RGB(A) samples, straight alpha, rows from the top, native byte order.
// Exit codes: 0 decoded, 1 not decoded (reason on standard error), 2 bad arguments,
// 3 over the pixel limit asked for.
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

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
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
constexpr int kHeaderWords = 7;
constexpr qint64 kMaxInputBytes = qint64(1) << 31; // as readWholeFile()
// Memory per pixel of a worker decode: GraphicsMagick's 16-bit RGBA cache and the answer in
// the worker, the answer and its copy here (fitsInMemory() adds the conversion's 16).
constexpr int kWorkerNativeBytesPerPixel = 8 + 8 + 8 + 8;
constexpr qint64 kWorkerBytesPerPixel = kWorkerNativeBytesPerPixel + 16;
constexpr qint64 kMaxIccBytes = qint64(1) << 26;
constexpr int kTimeoutMs = 30000;
constexpr int kExitDecoded = 0, kExitFailed = 1, kExitUsage = 2, kExitOverLimit = 3;

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

quint32 readWord(const QByteArray &bytes, qsizetype at)
{
    const auto *p = reinterpret_cast<const unsigned char *>(bytes.constData()) + at;
    return quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24;
}

constexpr qsizetype kHeaderBytes = qsizetype(sizeof kMagic) + kHeaderWords * 4;

// The answer's header, checked before anything else of it is used.
struct Answer {
    quint32 width = 0, height = 0, channels = 0, sampleBytes = 0, bits = 0, orientation = 1, iccBytes = 0;
    qint64 dataBytes = 0;
    qint64 totalBytes() const { return kHeaderBytes + qint64(iccBytes) + dataBytes; }
};

bool parseHeader(const QByteArray &answer, qint64 limit, Answer *a)
{
    if (answer.size() < kHeaderBytes || std::memcmp(answer.constData(), kMagic, sizeof kMagic) != 0)
        return false;
    a->width = readWord(answer, 8);
    a->height = readWord(answer, 12);
    a->channels = readWord(answer, 16);
    a->sampleBytes = readWord(answer, 20);
    a->bits = readWord(answer, 24);
    a->orientation = readWord(answer, 28);
    a->iccBytes = readWord(answer, 32);
    const qint64 pixels = qint64(a->width) * a->height;
    a->dataBytes = pixels * a->channels * a->sampleBytes;
    return a->width > 0 && a->height > 0 && pixels <= limit && (a->channels == 3 || a->channels == 4)
           && (a->sampleBytes == 1 || a->sampleBytes == 2) && a->bits > 0 && a->bits <= 16 && a->iccBytes <= kMaxIccBytes;
}

#ifdef IMAGEVIEWER_HAVE_GRAPHICSMAGICK
void writeWord(std::FILE *out, quint32 v)
{
    const unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8),
                                static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
    std::fwrite(b, 1, 4, out);
}

int fail(const char *what, const ExceptionInfo &exception)
{
    std::fprintf(stderr, "%s: %s%s%s\n", what, exception.reason ? exception.reason : "unknown error",
                 exception.description ? " " : "", exception.description ? exception.description : "");
    return kExitFailed;
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
    if (argc != 4 || !isAllowedCoder(argv[2])) {
        std::fprintf(stderr, "usage: ImageViewer --decode-worker <coder> <max pixels>\n");
        return kExitUsage;
    }
    const char *coder = argv[2];
    const long long maxPixels = std::atoll(argv[3]);
    if (maxPixels <= 0 || maxPixels > kMaxPixels) {
        std::fprintf(stderr, "invalid pixel limit\n");
        return kExitUsage;
    }
    std::vector<unsigned char> input;
    std::array<unsigned char, 65536> chunk;
    for (std::size_t n; (n = std::fread(chunk.data(), 1, chunk.size(), stdin)) > 0;) {
        if (qint64(input.size() + n) > kMaxInputBytes) {
            std::fprintf(stderr, "file too large\n");
            return kExitFailed;
        }
        input.insert(input.end(), chunk.begin(), chunk.begin() + std::ptrdiff_t(n));
    }
    if (input.empty()) {
        std::fprintf(stderr, "empty file\n");
        return kExitFailed;
    }
#ifdef IMAGEVIEWER_HAVE_GRAPHICSMAGICK
    // GraphicsMagick looks for its configuration files (delegates.mgk, log.mgk) around the
    // directory of the "client" it is given: an empty file in the private working directory
    // the viewer made for this decode (decodeInWorker()), so it finds none.
    const std::string client = (std::filesystem::current_path() / "bin" / "ImageViewer").string();
    InitializeMagick(std::filesystem::exists(client) ? client.c_str() : argv[0]);
    if (const char *why = keepOnlyAllowedCoders()) {
        std::fprintf(stderr, "cannot restrict the coders and delegates: %s\n", why);
        return kExitFailed;
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
        const int code = exception.severity == ResourceLimitError ? kExitOverLimit : fail("not decoded", exception);
        DestroyExceptionInfo(&exception);
        return code;
    }
    // GraphicsMagick's XCF reader returns the bottom layer on the canvas and the others as
    // the next images (its own compositing is disabled): composite them, each with its blend
    // mode and offset; hidden layers carry NoCompositeOp. Not FlattenImages(), which would
    // put the background colour under a transparent canvas.
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
        return kExitOverLimit;
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
    const int orientation = int(image->orientation) >= 1 && int(image->orientation) <= 8 ? int(image->orientation) : 1;
    std::fwrite(kMagic, 1, sizeof kMagic, stdout);
    for (quint32 word : {quint32(width), quint32(height), quint32(channels), quint32(sampleBytes),
                         quint32(std::min<unsigned>(image->depth, 16)), quint32(orientation), quint32(iccBytes)})
        writeWord(stdout, word);
    if (iccBytes)
        std::fwrite(icc, 1, iccBytes, stdout);
    std::fwrite(pixels.data(), 1, pixels.size(), stdout);
    DestroyImageList(image);
    DestroyExceptionInfo(&exception);
    return std::fflush(stdout) == 0 ? kExitDecoded : kExitFailed;
#else
    Q_UNUSED(coder);
    std::fprintf(stderr, "built without GraphicsMagick\n");
    return kExitFailed;
#endif
}

bool decodeInWorker(const Format &format, const QString &path, qint64 maxPixels, Decoded *out, QString *error)
{
    QByteArray bytes;
    if (!readWholeFile(path, &bytes, error))
        return false;
    if (bytes.isEmpty()) {
        *error = damaged(format.name);
        return false;
    }
    // The pixels asked for, within what this machine's memory can take (checked again with
    // the real size as soon as the answer's header arrives).
    const qint64 budget = decodeMemoryBudget();
    qint64 limit = maxPixels > 0 ? std::min(maxPixels, kMaxPixels) : kMaxPixels;
    if (budget > 0)
        limit = std::max<qint64>(1, std::min(limit, budget / kWorkerBytesPerPixel));
    QProcess worker;
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
    const QTemporaryDir sandbox;
    QFile client(sandbox.path() + QStringLiteral("/bin/ImageViewer"));
    if (!sandbox.isValid() || !QDir(sandbox.path()).mkdir(QStringLiteral("bin")) || !client.open(QIODevice::WriteOnly)) {
        *error = damaged(format.name);
        return false;
    }
    client.close();
    worker.setWorkingDirectory(sandbox.path());
    const QString place = QDir::toNativeSeparators(sandbox.path());
    for (const char *variable : {"MAGICK_TMPDIR", "TMPDIR", "TMP", "TEMP", "HOME", "MAGICK_CODER_MODULE_PATH"})
        environment.insert(QString::fromLatin1(variable), place);
    worker.setProcessEnvironment(environment);
    worker.setProgram(QCoreApplication::applicationFilePath());
    worker.setArguments({QStringLiteral("--decode-worker"), QString::fromLatin1(format.id).toUpper(), QString::number(limit)});
    worker.start();
    if (!worker.waitForStarted(kTimeoutMs)) {
        qCWarning(lcWorker) << "decode worker did not start:" << worker.errorString();
        *error = damaged(format.name);
        return false;
    }
    worker.write(bytes);
    worker.closeWriteChannel();
    bytes.clear();

    // The header comes first and says how much follows: anything else, or more, means a
    // broken worker, which is stopped.
    QByteArray answer;
    QByteArray messages;
    Answer a;
    QString tooLarge;
    bool haveHeader = false, broken = false, timedOut = false;
    QElapsedTimer clock;
    clock.start();
    const auto collect = [&] {
        answer += worker.readAllStandardOutput();
        const QByteArray said = worker.readAllStandardError(); // always drained: QProcess would keep it all
        if (messages.size() < 4096)
            messages += said.left(4096 - messages.size());
        if (!haveHeader && answer.size() >= kHeaderBytes) {
            haveHeader = parseHeader(answer, limit, &a);
            broken = !haveHeader;
            // The real size is known: no use waiting for an image this machine cannot take.
            if (haveHeader && !fitsInMemory(qint64(a.width) * a.height, kWorkerNativeBytesPerPixel, &tooLarge))
                broken = true;
        }
        if (haveHeader && answer.size() > a.totalBytes())
            broken = true;
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
    if (worker.exitCode() == kExitOverLimit) {
        // Over the pixels asked for (preloading skips it), or over this machine's memory.
        if (maxPixels > 0)
            out->overLimit = true;
        else
            *error = QCoreApplication::translate("Image", "Not enough memory to decode the image.");
        return false;
    }
    if (worker.exitCode() != kExitDecoded || !haveHeader || answer.size() != a.totalBytes()) {
        qCInfo(lcWorker) << format.name << "not decoded:" << worker.exitCode() << messages.trimmed();
        *error = damaged(format.name);
        return false;
    }
    const qint64 pixels = qint64(a.width) * a.height;
    if (!fitsInMemory(pixels, int(a.channels * a.sampleBytes), error))
        return false;
    out->width = int(a.width);
    out->height = int(a.height);
    out->sample = a.sampleBytes == 2 ? Decoded::Sample::U16 : Decoded::Sample::U8;
    out->channels = int(a.channels);
    out->sourceChannels = int(a.channels);
    out->alphaIndex = a.channels == 4 ? 3 : -1;
    out->bits = int(a.bits);
    out->orientation = a.orientation >= 1 && a.orientation <= 8 ? int(a.orientation) : 1;
    out->codec = QStringLiteral("GraphicsMagick %1 (worker)").arg(QString::fromLatin1(format.id).toUpper());
    if (a.iccBytes > 0)
        describeIcc(answer.mid(kHeaderBytes, a.iccBytes), &out->colour);
    else
        assumeDefault(false, &out->colour);
    out->data.reset(new unsigned char[std::size_t(a.dataBytes)]);
    std::memcpy(out->data.get(), answer.constData() + kHeaderBytes + a.iccBytes, std::size_t(a.dataBytes));
    return true;
}
