// The information panel's histogram (D-115): how many pixels hold each value, per channel and for
// luminance, on the axis of the image's own signal, what the file stores: its sRGB or Display P3
// codes, PQ or HLG for HDR. Counted while the image is decoded (image.cpp), from the decoder's own
// samples, before any conversion: exact, where the working buffer (half floats in scRGB) could not
// give a saturated wide-gamut colour's small components back. Linear sources (EXR, float TIFF) are
// counted on PQ, which holds HDR, and CMYK ones on the sRGB curve, from the converted colours.
// Drawn as macOS Preview draws its own (the owner's reference): the three colours' areas added, or
// the luminance's.
#pragma once

#include "color.h"

#include <QColor>
#include <QRectF>
#include <QString>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

class QPainter;

struct Histogram {
    static constexpr int kBins = 256;
    enum Channel { Red, Green, Blue, Luminance, ChannelCount };

    std::array<std::array<quint32, kBins>, ChannelCount> bins{};
    quint64 pixels = 0;         // counted: fully transparent pixels have no colour and are left out
    // SDR white on the axis (0 to 1), when it is not at its end (PQ, HLG); < 0: none.
    double referenceWhite = -1.0;
    QString axis;               // the signal counted: "sRGB", "PQ", "HLG", "ICC"... (informative)

    bool isEmpty() const { return pixels == 0; }
    void add(const Histogram &other); // another band's counts
};

// Counts a decode's pixels: set up once for an image from what its decoder gives, then given each
// band of pixels on the band's own thread (image.cpp converts in parallel); the bands' histograms
// are added at the end.
class HistogramCounter {
public:
    // `colour` and `cmyk` as the decoder gives them; `containerBits`: 8 or 16 for integer samples
    // (normalised to 0..1 by the container's range), 0 for floats.
    HistogramCounter(const color::Descriptor &colour, bool cmyk, int containerBits);
    HistogramCounter(const HistogramCounter &) = delete;
    HistogramCounter &operator=(const HistogramCounter &) = delete;

    // A histogram with nothing counted yet, its axis set.
    Histogram empty() const;
    // A band: first its samples as the decoder gives them (straight RGBA, normalised; gray as R = G
    // = B), then the same pixels converted (linear scRGB, straight alpha).
    void countSignal(const float *rgba, std::size_t pixels, Histogram *into) const;
    void countLinear(const float *rgba, std::size_t pixels, Histogram *into) const;

private:
    int lookUp(float v) const; // a half float's bin on the axis
    bool m_native = false;     // the colours from the file's own samples
    bool m_narrow = false;     // ...in the narrow (video) range
    double m_maxCode = 255.0;
    double m_blackCode = 0.0, m_codeRange = 255.0;
    bool m_separable = true;   // the luminance through a table (not HLG)
    std::unique_ptr<color::Encoder> m_encoder;
    std::vector<quint8> m_tables; // a bin for each half float, on the axis
    double m_referenceWhite = -1.0;
    QString m_axis;
};

// Draws `histogram` in `rect` on `plot`: with `colors`, the red, green and blue areas added (where
// all three meet they add up to grey) with an outline each; otherwise the luminance's in `ink`. The
// tallest bin inside the axis fills the height; a taller one at either end (clipped shadows or
// highlights) is cut at the top. SDR white, when it is inside the axis, is a dotted line.
void drawHistogram(QPainter &painter, const QRectF &rect, const Histogram &histogram, bool colors, const QColor &ink,
                   const QColor &plot);
