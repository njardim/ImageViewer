// Decoder back ends (decision D-38). Each one turns a file into native samples with straight
// alpha (D-21) and a colour descriptor, and converts no colour itself: image.cpp converts
// once into linear scRGB. The registry (formats.h) decides which back end reads a file.
#pragma once

#include "formats.h"
#include "image.h"

#include <QLoggingCategory>
#include <QRect>
#include <QString>

#include <atomic>
#include <memory>
#include <string>

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
    color::Descriptor colour;
    QString codec;
    CameraInfo camera;
    bool overLimit = false;        // larger than the pixel limit asked for: nothing was read
    int durationMs = 0;            // how long this frame is shown, in an animation
    // A vector image's own size (an SVG's, in CSS pixels) when it was drawn at another (D-90): the
    // panel and the zoom speak of the document's size. 0: the same as width and height.
    int documentWidth = 0;
    int documentHeight = 0;
    double documentScale = 0.0; // and the scale it was drawn at: texels per document pixel
    bool vector = false; // drawn from a description (SVG): it can be drawn again at any scale (D-93)

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

// Refuses absurd dimensions before anything else.
inline constexpr qint64 kMaxPixels = qint64(1) << 30;
// The largest file read into memory (readWholeFile(), the decode worker's input).
inline constexpr qint64 kMaxFileBytes = qint64(1) << 31;

// The frames of an animated file (E6), composited to full canvases and read in order; every
// frame has the first frame's size, samples and channel layout. One user at a time.
class FrameReader {
public:
    virtual ~FrameReader() = default;
    // The next frame into `out` and how long it is shown. False after the last frame (with
    // *error left empty) or when the file cannot be read (*error set).
    virtual bool next(Decoded *out, int *durationMs, QString *error) = 0;
    // Back before the first frame.
    virtual bool rewind(QString *error) = 0;
    // Number of frames; 0 while unknown (known once the last frame has been read).
    virtual int frameCount() const = 0;
    // How many times the animation plays; 0 = forever.
    virtual int loopCount() const = 0;
};

// What a decode is asked for beyond its pixel limit.
// A vector image (SVG) is drawn on its longer side at its own size (D-90), made smaller to fit
// `maxSide` (the GPU's largest texture); with `fill`, at exactly `maxSide` (a thumbnail, drawn at
// its own size rather than reduced or enlarged afterwards); 0: its own size. Without `fill`, never
// larger than fits in `view` (the window, device pixels), the size it is shown at (D-94). With
// `scale` > 0 instead, only `area` of it, in pixels of the document drawn at `scale` (the zoom's
// detail, D-93): a whole number of pixels from an origin that may fall between two (D-94).
// Setting `cancel` during a decode in a worker process (SVG, GraphicsMagick) ends that process,
// and the decode fails (D-93).
struct DecodeOptions {
    int maxSide = 0;
    bool fill = false;
    QSize view;
    double scale = 0.0;
    QRectF area;
    const std::atomic_bool *cancel = nullptr;
};

// Detects the format (formats.h) and decodes with its back end, then with the general ones
// (OpenImageIO, Qt) if that fails. With `maxPixels` > 0 a larger source is not read
// (out->overLimit). False with *error set (the first back end's reason) on failure.
// For an animated file, *frames (when given) receives the reader of the frames after the
// first, and out->durationMs the first frame's display time.
bool decodeFile(const QString &path, qint64 maxPixels, Decoded *out, QString *error,
                std::unique_ptr<FrameReader> *frames = nullptr, DecodeOptions options = {});

// Stops OpenImageIO's worker threads (see shutdownDecoders()).
void shutdownOpenImageIO();

// Shared by the back ends (decoders.cpp, codecs.cpp).
// Refuses a decode that would need more than ~60 % of physical memory (*error says why).
bool fitsInMemory(qint64 pixels, int nativeBytesPerPixel, QString *error);
// What every back end checks before it reads a pixel (D-38): dimensions within kMaxPixels, a
// volume's slices included (*error otherwise, naming `channels`); the pixel limit asked for
// (out->overLimit otherwise); fitsInMemory() for every slice. Any 32-bit dimension is safe.
bool checkDimensions(qint64 width, qint64 height, int channels, int nativeBytesPerPixel, qint64 maxPixels, Decoded *out,
                     QString *error, qint64 depth = 1);
// That ~60 % of physical memory, in bytes (0 if unknown).
qint64 decodeMemoryBudget();
// "damaged, truncated or unsupported <format> file", for every back end.
QString damaged(const char *formatName);
Q_DECLARE_LOGGING_CATEGORY(lcDecode) // imageviewer.decode (image.cpp)
// A library's own reason (OpenImageIO, Qt's readers) goes to the log, never to the interface
// (D-64): the interface says it in its own words. Returns damaged(format), or an empty string
// for decodeFile() to name the format when the format is not known here.
QString libraryError(const char *library, const std::string &message, const std::string &format = {});
// Shooting data, and the orientation when asked, from an EXIF block (TIFF header first).
CameraInfo cameraFromExif(QByteArrayView tiff, int *orientation);
// Text from a file (EXIF, an ICC profile's description, a colour space's name) as the panels show
// it: no control or format characters (line breaks, bidirectional overrides), one line, at most
// 64 characters.
QString cleanText(QString text);
// The colour of an embedded ICC profile, described by the profile's own text.
void describeIcc(const QByteArray &icc, color::Descriptor *d);
// sRGB for integer samples, linear BT.709 for float, marked as assumed (F12).
void assumeDefault(bool isFloat, color::Descriptor *d);
// A frame's display time: browsers play 10 ms or less at 100 ms; at most a minute.
int playableMs(double ms);
// The first frame's layout (size, samples, channels, colour) on a later frame.
void copyLayout(const Decoded &from, Decoded *to);
std::size_t frameBytes(const Decoded &d);
// The whole file in memory (read, not mapped: see decoders.cpp).
bool readWholeFile(const QString &path, QByteArray *bytes, QString *error);
// OpenImageIO on a file in memory; `name` only tells it the format (e.g. "frame.png").
bool decodeOiioMemory(QByteArrayView bytes, const char *name, Decoded *out, QString *error, qint64 maxPixels = 0);

// worker.cpp: the decode worker process (D-38), for GraphicsMagick and SVG (svg.cpp).
bool graphicsMagickAvailable();
// The `--decode-worker` mode of the executable (main.cpp starts it before anything else).
int runDecodeWorker(int argc, char **argv);
// Decodes `path` in a worker process: GraphicsMagick's coder for the format, or the SVG renderer
// as `options` asks. Never crashes the caller.
bool decodeInWorker(const Format &format, const QString &path, qint64 maxPixels, Decoded *out, QString *error,
                    DecodeOptions options = {});
// Ends this thread's SVG worker once it has had no request for 30 s (D-94): a decoding thread busy
// with other formats keeps no idle worker; one with nothing to do ends after QThreadPool's timeout.
void stopIdleSvgWorker();
// The worker's exit codes: decoded, not decoded (the reason on standard error), bad arguments,
// over the pixel limit asked for (or out of memory).
inline constexpr int kWorkerDecoded = 0, kWorkerFailed = 1, kWorkerUsage = 2, kWorkerOverLimit = 3;
// The worker's answer on standard output (worker.cpp has its layout). `icc` holds iccBytes bytes,
// `pixels` width x height x channels samples. Returns the exit code.
struct WorkerAnswer {
    quint32 width = 0, height = 0, channels = 0, sampleBytes = 0, bits = 0, orientation = 1, iccBytes = 0;
    bool associatedAlpha = false;                // colour multiplied by alpha already
    quint32 documentWidth = 0, documentHeight = 0; // Decoded::documentWidth, documentHeight
    double documentScale = 0.0;                    // Decoded::documentScale
};
int writeWorkerAnswer(const WorkerAnswer &answer, const unsigned char *icc, const unsigned char *pixels);

// svg.cpp: SVG through the renderer behind imageviewer_svg.h (D-90), in the decode worker only.
bool svgAvailable();
// The renderer's name and version, e.g. "resvg 0.48.1"; empty when the build has none.
QString svgRenderer();
// The worker's SVG mode: `file` is the file's bytes, an svgz inflated here. Returns the exit code.
int runSvgWorker(QByteArrayView file, qint64 maxPixels, DecodeOptions options);

// syscodecs.cpp: the operating system's HEIC decoder (D-39).
// What to do when the system has no HEVC decoder (Windows: the Microsoft Store extensions).
QString missingHevcMessage();
bool systemHeicAvailable();
bool decodeSystemHeic(const QString &path, qint64 maxPixels, Decoded *out, QString *error);

// heif.cpp: AVIF image sequences through libheif (1.23 or later at build time).
bool heifSequencesAvailable();
// The colour of a HEIF/AVIF still image (CICP, then ICC) from libheif; false when it has neither
// or libheif is not linked. OpenImageIO reads no ICC profile from these files, and no nclx box
// when an ICC profile is also there.
bool describeHeifStill(QByteArrayView bytes, color::Descriptor *d);
bool decodeHeifSequence(QByteArray bytes, qint64 maxPixels, Decoded *out, QString *error,
                        std::unique_ptr<FrameReader> *frames);

// qtimage.cpp: the formats the registry gives to Qt (Apple icons, cursors, XPM, XBM, WBMP), each
// through its own plugin only.
bool decodeWithQt(const QString &path, const Format *format, qint64 maxPixels, Decoded *out, QString *error);

// codecs.cpp: libjxl, libwebp and Softimage PIC (D-45), which parse the file from memory; APNG
// is handed to apng.cpp.
bool codecAvailable(Decoder decoder);
bool decodeCodec(Decoder decoder, QByteArray bytes, qint64 maxPixels, Decoded *out, QString *error,
                 std::unique_ptr<FrameReader> *frames);

// apng.cpp: animated PNG; a PNG without animation control is read as a plain PNG.
bool decodeApng(QByteArray bytes, qint64 maxPixels, Decoded *out, QString *error,
                std::unique_ptr<FrameReader> *frames);

// Settings > Image Formats (D-60, D-61): a decoder's name and the version this build uses, e.g.
// "OpenImageIO" and "3.2.1.1". The libraries' own versions come from the files that include them
// (codecs.cpp, heif.cpp, worker.cpp); empty when the library is not built in.
QString decoderName(Decoder decoder);
QString decoderVersion(Decoder decoder);
QString jxlVersion();
QString webpVersion();
QString heifVersion();
QString graphicsMagickVersion();
