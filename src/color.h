// Colour science used by the decoders: colour descriptors, transfer functions,
// primaries and the single conversion step into the working space.
//
// Working space (decision D-10): linear scRGB — BT.709/sRGB primaries, D65,
// linear light, extended range (values < 0 and > 1 allowed), 1.0 = SDR
// reference white. Absolute HDR signals (PQ, HLG) are mapped so that
// kSdrReferenceWhiteNits lands on 1.0 (ITU-R BT.2408).
#pragma once

#include <QByteArray>
#include <QString>

#include <array>
#include <cstddef>
#include <vector>

namespace color {

inline constexpr float kSdrReferenceWhiteNits = 203.0f; // ITU-R BT.2408
inline constexpr float kHlgNominalPeakNits = 1000.0f;   // BT.2100 reference display

enum class Transfer {
    Linear,
    Srgb,   // IEC 61966-2-1
    Bt1886, // gamma 2.4 display EOTF (H.273 codes 1, 6, 14, 15; decision D-12)
    Power,  // pure power law with exponent Descriptor::gamma (H.273 4 and 5, PNG gAMA, OIIO gNN ids)
    Pq,     // SMPTE ST 2084, absolute luminance
    Hlg,    // ARIB STD-B67 / BT.2100 HLG
};

// CIE xy chromaticities of the red, green, blue primaries and the white point.
struct Chromaticities {
    double r[2], g[2], b[2], w[2];
};

inline constexpr Chromaticities kBt709 = {{0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}, {0.3127, 0.3290}};
inline constexpr Chromaticities kBt2020 = {{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, {0.3127, 0.3290}};
inline constexpr Chromaticities kDisplayP3 = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.3127, 0.3290}};
inline constexpr Chromaticities kAdobeRgb = {{0.640, 0.330}, {0.210, 0.710}, {0.150, 0.060}, {0.3127, 0.3290}};
inline constexpr Chromaticities kAcesAp0 = {{0.7347, 0.2653}, {0.0, 1.0}, {0.0001, -0.0770}, {0.32168, 0.33767}};
inline constexpr Chromaticities kAcesAp1 = {{0.713, 0.293}, {0.165, 0.830}, {0.128, 0.044}, {0.32168, 0.33767}};

// Describes how the decoded RGB(A) values must be interpreted.
struct Descriptor {
    enum class Source { Assumed, Icc, Cicp, FormatAttributes };
    Source source = Source::Assumed;
    QByteArray icc;                     // valid when source == Icc
    Chromaticities primaries = kBt709;  // used when source != Icc
    Transfer transfer = Transfer::Srgb; // used when source != Icc
    float gamma = 2.2f;                 // exponent when transfer == Power
    bool fullRange = true;              // false: integer codes use the narrow (video) range, H.273
    QString description;                // human readable, for the info panel

    bool isHdr() const { return source != Source::Icc && (transfer == Transfer::Pq || transfer == Transfer::Hlg); }
    // PQ encodes absolute luminance; everything else is relative to the display's white.
    bool isAbsolute() const { return source != Source::Icc && transfer == Transfer::Pq; }
};

// ITU-T H.273 code points -> descriptor fields. Return false for codes we do not handle.
bool primariesFromCicp(int code, Chromaticities *out);
// False for chromaticities that give no invertible RGB -> XYZ matrix (degenerate file metadata).
bool isUsable(const Chromaticities &c);
bool transferFromCicp(int code, Transfer *out, float *gamma);
QString transferName(Transfer t, float gamma);

// Human-readable description stored in an ICC profile (empty if unreadable).
QString iccDescription(const QByteArray &icc);

// Scalar transfer functions (signal in [0,1] unless stated otherwise).
float srgbToLinear(float v);  // extended: odd-symmetric for v < 0
float linearToSrgb(float v);
float pqToNits(float v);      // returns cd/m2 (0..10000)
float nitsToPq(float nits);
float bt1886ToLinear(float v);

using Matrix3 = std::array<double, 9>; // row-major

// RGB -> XYZ for the given chromaticities (white normalised to Y = 1).
Matrix3 rgbToXyz(const Chromaticities &c);
// Linear RGB in `from` -> linear RGB in `to`, with Bradford adaptation if the white points differ.
Matrix3 rgbToRgb(const Chromaticities &from, const Chromaticities &to);

// Luminance of linear scRGB (BT.709 primaries).
inline float luminance(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

// ITU-R BT.2390 EETF (§5.4.1) with zero black levels, evaluated in the PQ domain.
// Maps luminance in [0, sourcePeak] to [0, targetPeak]; identity up to the knee.
float eetfBt2390(float nits, float sourcePeakNits, float targetPeakNits);
float eetfKneeNits(float sourcePeakNits, float targetPeakNits);

// Output stage of the pipeline (docs/PLAN.md §6.2): exposure, scale to output
// units, tone mapping or clip, encoding. src/shaders/image.frag implements the
// same arithmetic on the GPU; applyOutputStage() is the reference used by the
// fidelity harness (`--render`).
enum class OutputEncoding { Sdr = 0, ScRgb = 1, Pq = 2 };

struct OutputStage {
    OutputEncoding encoding = OutputEncoding::Sdr;
    float exposure = 1.0f;       // linear multiplier
    float scale = 1.0f;          // output units per working unit
    float peak = 1.0f;           // brightest output value, in output units
    float nitsPerUnit = kSdrReferenceWhiteNits; // luminance of one output unit, for the EETF
    float sourcePeak = 0.0f;     // > peak: BT.2390 EETF from this content peak (output units); otherwise clip
    bool clipWarning = false;    // paint values that are not reproduced exactly
    float background[3] = {0.0f, 0.0f, 0.0f}; // linear, output units; translucent pixels composite over it
};

// The PQ curve (and therefore the EETF) ends at 10 000 cd/m2.
inline constexpr float kPqPeakNits = 10000.0f;

// Premultiplied linear scRGB in; encoded output values, composited over the
// background in linear light, out (alpha becomes 1).
void applyOutputStage(const OutputStage &stage, float *rgba);

// Encodes linear output units (BT.709 primaries) for the output, in place.
void encodeOutput(OutputEncoding encoding, float *rgb);

// Prepared conversion from the colour encoding described by a Descriptor into
// linear scRGB. Build once per image; apply() is thread-safe and may be called
// concurrently on disjoint pixel ranges.
//
// Three paths: analytic (CICP and format attributes); ICC matrix/TRC profiles
// (almost every camera and display profile), evaluated as per-channel curves
// plus one 3x3 matrix, which is exact for integer sources and keeps parametric
// curves unbounded; and LittleCMS for every other ICC profile (LUT-based, or
// with a non-zero black point that black-point compensation would move).
class Converter {
public:
    // `integerSourceBits` (8 or 16) enables exact lookup tables for integer
    // sources whose samples were normalised to [0, 1]; pass 0 for float data.
    Converter(const Descriptor &d, int integerSourceBits);
    ~Converter();
    Converter(const Converter &) = delete;
    Converter &operator=(const Converter &) = delete;

    // Non-empty when the descriptor cannot be honoured (e.g. unusable ICC profile).
    const QString &error() const { return m_error; }
    // True when LittleCMS does the work (diagnostics and tests).
    bool usesLittleCms() const { return m_iccTransform != nullptr; }

    // Converts straight-alpha RGBA float pixels in place; alpha is untouched.
    void apply(float *rgba, std::size_t pixelCount) const;

private:
    bool initMatrixShaper(const QByteArray &icc, int integerSourceBits);
    float decode(int channel, float v) const; // per-channel transfer, float sources

    Transfer m_transfer = Transfer::Srgb;
    float m_gamma = 2.2f;
    void *m_iccTransform = nullptr;     // cmsHTRANSFORM (general ICC path)
    std::array<void *, 3> m_curves{};   // cmsToneCurve * (matrix/TRC path)
    std::vector<float> m_lut;           // 3 x 65536 entries for integer sources
    bool m_convertPrimaries = false;
    float m_matrix[9] = {};
    float m_lumaWeights[3] = {};        // source-primaries luminance, for the HLG OOTF
    QString m_error;
};

} // namespace color
