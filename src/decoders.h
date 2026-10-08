// Decoder back ends (decision D-38). Each one turns a file into native samples with straight
// alpha (D-21) and a colour descriptor, and converts no colour itself: image.cpp converts
// once into linear scRGB. The registry (formats.h) decides which back end reads a file.
#pragma once

#include "formats.h"
#include "image.h"

#include <QString>

#include <memory>

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

// Detects the format (formats.h) and decodes with its back end, then with the general ones
// (OpenImageIO, Qt) if that fails. With `maxPixels` > 0 a larger source is not read
// (out->overLimit). False with *error set (the first back end's reason) on failure.
// For an animated file, *frames (when given) receives the reader of the frames after the
// first, and out->durationMs the first frame's display time.
bool decodeFile(const QString &path, qint64 maxPixels, Decoded *out, QString *error,
                std::unique_ptr<FrameReader> *frames = nullptr);

// Stops OpenImageIO's worker threads (see shutdownDecoders()).
void shutdownOpenImageIO();

// Shared by the back ends (decoders.cpp, codecs.cpp).
// Refuses a decode that would need more than ~60 % of physical memory (*error says why).
bool fitsInMemory(qint64 pixels, int nativeBytesPerPixel, QString *error);
// "damaged, truncated or unsupported <format> file", for every back end.
QString damaged(const char *formatName);
// Shooting data, and the orientation when asked, from an EXIF block (TIFF header first).
CameraInfo cameraFromExif(QByteArrayView tiff, int *orientation);
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
bool decodeOiioMemory(QByteArrayView bytes, const char *name, Decoded *out, QString *error);

// syscodecs.cpp: the operating system's HEIC decoder (D-39).
// What to do when the system has no HEVC decoder (Windows: the Microsoft Store extensions).
QString missingHevcMessage();
bool systemHeicAvailable();
bool decodeSystemHeic(const QString &path, qint64 maxPixels, Decoded *out, QString *error);

// heif.cpp: AVIF image sequences through libheif (1.23 or later at build time).
bool heifSequencesAvailable();
bool decodeHeifSequence(QByteArray bytes, qint64 maxPixels, Decoded *out, QString *error,
                        std::unique_ptr<FrameReader> *frames);

// codecs.cpp: libjxl, libwebp and APNG, which parse the file from memory.
bool codecAvailable(Decoder decoder);
bool decodeCodec(Decoder decoder, QByteArray bytes, qint64 maxPixels, Decoded *out, QString *error,
                 std::unique_ptr<FrameReader> *frames);
