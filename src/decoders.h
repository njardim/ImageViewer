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

// Detects the format (formats.h) and decodes with its back end, then with the general ones
// (OpenImageIO, Qt) if that fails. With `maxPixels` > 0 a larger source is not read
// (out->overLimit). False with *error set (the first back end's reason) on failure.
bool decodeFile(const QString &path, qint64 maxPixels, Decoded *out, QString *error);

// Stops OpenImageIO's worker threads (see shutdownDecoders()).
void shutdownOpenImageIO();
