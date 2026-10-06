#include "color.h"

#include <lcms2.h>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <vector>

namespace color {
namespace {

Matrix3 multiply(const Matrix3 &a, const Matrix3 &b)
{
    Matrix3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    return r;
}

Matrix3 inverse(const Matrix3 &m)
{
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    const double s = 1.0 / det;
    return {A * s, -(b * i - c * h) * s, (b * f - c * e) * s,
            B * s, (a * i - c * g) * s, -(a * f - c * d) * s,
            C * s, -(a * h - b * g) * s, (a * e - b * d) * s};
}

std::array<double, 3> xyToXyz(const double xy[2])
{
    return {xy[0] / xy[1], 1.0, (1.0 - xy[0] - xy[1]) / xy[1]};
}

bool sameWhite(const Chromaticities &a, const Chromaticities &b)
{
    return std::abs(a.w[0] - b.w[0]) < 1e-4 && std::abs(a.w[1] - b.w[1]) < 1e-4;
}

bool samePrimaries(const Chromaticities &a, const Chromaticities &b)
{
    auto near = [](const double p[2], const double q[2]) {
        return std::abs(p[0] - q[0]) < 1e-4 && std::abs(p[1] - q[1]) < 1e-4;
    };
    return near(a.r, b.r) && near(a.g, b.g) && near(a.b, b.b) && near(a.w, b.w);
}

float hlgInverseOetf(float v)
{
    constexpr float a = 0.17883277f, b = 0.28466892f, c = 0.55991073f;
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.5f ? v * v / 3.0f : (std::exp((v - c) / a) + b) / 12.0f;
}

float power(float v, float gamma)
{
    return v < 0 ? -std::pow(-v, gamma) : std::pow(v, gamma);
}

// Per-channel decode: relative linear light (1.0 = SDR reference white), except
// HLG, which yields scene light in [0, 1]; its OOTF needs all three channels.
float decodeChannel(Transfer t, float gamma, float v)
{
    switch (t) {
    case Transfer::Linear: return v;
    case Transfer::Srgb: return srgbToLinear(v);
    case Transfer::Bt1886: return bt1886ToLinear(v);
    case Transfer::Power: return power(v, gamma);
    case Transfer::Pq: return pqToNits(v) / kSdrReferenceWhiteNits;
    case Transfer::Hlg: return hlgInverseOetf(v);
    }
    return v;
}

// D50 as LittleCMS uses it for the ICC profile connection space (cmsD50_XYZ).
constexpr std::array<double, 3> kD50 = {0.9642, 1.0, 0.8249};

// Bradford chromatic adaptation from white `ws` to white `wd` (XYZ, Y = 1).
Matrix3 bradford(const std::array<double, 3> &ws, const std::array<double, 3> &wd)
{
    const Matrix3 cone = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    auto apply = [&](const std::array<double, 3> &w) {
        return std::array<double, 3>{cone[0] * w[0] + cone[1] * w[1] + cone[2] * w[2],
                                     cone[3] * w[0] + cone[4] * w[1] + cone[5] * w[2],
                                     cone[6] * w[0] + cone[7] * w[1] + cone[8] * w[2]};
    };
    const auto cs = apply(ws), cd = apply(wd);
    const Matrix3 scale = {cd[0] / cs[0], 0, 0, 0, cd[1] / cs[1], 0, 0, 0, cd[2] / cs[2]};
    return multiply(inverse(cone), multiply(scale, cone));
}

cmsHPROFILE createLinearScRgbProfile()
{
    cmsCIExyY d65 = {0.3127, 0.3290, 1.0};
    cmsCIExyYTRIPLE primaries = {{0.640, 0.330, 1.0}, {0.300, 0.600, 1.0}, {0.150, 0.060, 1.0}};
    cmsToneCurve *linear = cmsBuildGamma(nullptr, 1.0);
    cmsToneCurve *curves[3] = {linear, linear, linear};
    cmsHPROFILE profile = cmsCreateRGBProfile(&d65, &primaries, curves);
    cmsFreeToneCurve(linear);
    return profile;
}

cmsHTRANSFORM createIccTransform(const QByteArray &icc, QString *error)
{
    cmsHPROFILE in = cmsOpenProfileFromMem(icc.constData(), cmsUInt32Number(icc.size()));
    if (!in) {
        *error = QStringLiteral("invalid ICC profile");
        return nullptr;
    }
    cmsUInt32Number inFormat = 0;
    const cmsColorSpaceSignature space = cmsGetColorSpace(in);
    if (space == cmsSigRgbData)
        inFormat = TYPE_RGBA_FLT; // alpha travels as an extra channel and is not touched
    else if (space == cmsSigGrayData)
        inFormat = FLOAT_SH(1) | COLORSPACE_SH(PT_GRAY) | EXTRA_SH(3) | CHANNELS_SH(1) | BYTES_SH(4);
    if (!inFormat) {
        cmsCloseProfile(in);
        *error = QStringLiteral("unsupported ICC colour space for RGBA data");
        return nullptr;
    }
    cmsHPROFILE out = createLinearScRgbProfile();
    // NOOPTIMIZE keeps the float pipeline unbounded, so colours outside the
    // BT.709 gamut survive as negative / >1 scRGB values (needed for EDR/HDR).
    // NOCACHE makes cmsDoTransform safe to call from several threads.
    cmsHTRANSFORM xf = cmsCreateTransform(in, inFormat, out, TYPE_RGB_FLT, INTENT_RELATIVE_COLORIMETRIC,
                                          cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOCACHE | cmsFLAGS_NOOPTIMIZE);
    cmsCloseProfile(in);
    cmsCloseProfile(out);
    if (!xf)
        *error = QStringLiteral("cannot build colour transform from ICC profile");
    return xf;
}

} // namespace

bool primariesFromCicp(int code, Chromaticities *out)
{
    switch (code) {
    case 1: *out = kBt709; return true;
    case 5: *out = {{0.640, 0.330}, {0.290, 0.600}, {0.150, 0.060}, {0.3127, 0.3290}}; return true; // BT.470 BG
    case 6:
    case 7: *out = {{0.630, 0.340}, {0.310, 0.595}, {0.155, 0.070}, {0.3127, 0.3290}}; return true; // SMPTE 170M/240M
    case 9: *out = kBt2020; return true;
    case 11: *out = {{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.314, 0.351}}; return true; // DCI-P3
    case 12: *out = kDisplayP3; return true;
    default: return false;
    }
}

bool transferFromCicp(int code, Transfer *out, float *gamma)
{
    switch (code) {
    case 1:
    case 6:
    case 14:
    case 15: *out = Transfer::Bt1886; return true;
    case 4: *out = Transfer::Power; *gamma = 2.2f; return true;
    case 5: *out = Transfer::Power; *gamma = 2.8f; return true;
    case 8: *out = Transfer::Linear; return true;
    case 13: *out = Transfer::Srgb; return true;
    case 16: *out = Transfer::Pq; return true;
    case 18: *out = Transfer::Hlg; return true;
    default: return false;
    }
}

QString transferName(Transfer t, float gamma)
{
    switch (t) {
    case Transfer::Linear: return QStringLiteral("linear");
    case Transfer::Srgb: return QStringLiteral("sRGB");
    case Transfer::Bt1886: return QStringLiteral("BT.1886 (γ2.4)");
    case Transfer::Power: return QStringLiteral("γ%1").arg(double(gamma), 0, 'g', 3);
    case Transfer::Pq: return QStringLiteral("PQ (ST 2084)");
    case Transfer::Hlg: return QStringLiteral("HLG");
    }
    return {};
}

QString iccDescription(const QByteArray &icc)
{
    cmsHPROFILE profile = cmsOpenProfileFromMem(icc.constData(), cmsUInt32Number(icc.size()));
    if (!profile)
        return {};
    wchar_t buffer[256] = {};
    // The size is in bytes, not characters (wchar_t is 4 bytes on Linux and macOS).
    const cmsUInt32Number n = cmsGetProfileInfo(profile, cmsInfoDescription, "en", "US", buffer, sizeof buffer - sizeof(wchar_t));
    cmsCloseProfile(profile);
    return n ? QString::fromWCharArray(buffer).trimmed() : QString();
}

float srgbToLinear(float v)
{
    const float a = std::abs(v);
    const float l = a <= 0.04045f ? a / 12.92f : std::pow((a + 0.055f) / 1.055f, 2.4f);
    return v < 0 ? -l : l;
}

float linearToSrgb(float v)
{
    const float a = std::abs(v);
    const float e = a <= 0.0031308f ? a * 12.92f : 1.055f * std::pow(a, 1.0f / 2.4f) - 0.055f;
    return v < 0 ? -e : e;
}

float bt1886ToLinear(float v)
{
    return v < 0 ? -std::pow(-v, 2.4f) : std::pow(v, 2.4f);
}

namespace {
constexpr double kPqM1 = 2610.0 / 16384.0;
constexpr double kPqM2 = 2523.0 / 4096.0 * 128.0;
constexpr double kPqC1 = 3424.0 / 4096.0;
constexpr double kPqC2 = 2413.0 / 4096.0 * 32.0;
constexpr double kPqC3 = 2392.0 / 4096.0 * 32.0;
} // namespace

float pqToNits(float v)
{
    const double e = std::pow(std::clamp(double(v), 0.0, 1.0), 1.0 / kPqM2);
    const double y = std::pow(std::max(e - kPqC1, 0.0) / (kPqC2 - kPqC3 * e), 1.0 / kPqM1);
    return float(10000.0 * y);
}

float nitsToPq(float nits)
{
    const double y = std::pow(std::clamp(double(nits) / 10000.0, 0.0, 1.0), kPqM1);
    return float(std::pow((kPqC1 + kPqC2 * y) / (1.0 + kPqC3 * y), kPqM2));
}

float eetfBt2390(float nits, float sourcePeakNits, float targetPeakNits)
{
    if (!(sourcePeakNits > targetPeakNits) || nits <= 0.0f)
        return nits;
    const double source = nitsToPq(sourcePeakNits);
    const double maxLum = nitsToPq(targetPeakNits) / source;
    const double knee = std::max(1.5 * maxLum - 0.5, 0.0);
    const double e1 = nitsToPq(nits) / source;
    if (e1 <= knee)
        return nits;
    const double t = std::min((e1 - knee) / (1.0 - knee), 1.0);
    const double t2 = t * t, t3 = t2 * t;
    const double e2 = (2 * t3 - 3 * t2 + 1) * knee + (t3 - 2 * t2 + t) * (1 - knee) + (-2 * t3 + 3 * t2) * maxLum;
    return pqToNits(float(e2 * source));
}

float eetfKneeNits(float sourcePeakNits, float targetPeakNits)
{
    if (!(sourcePeakNits > targetPeakNits))
        return sourcePeakNits;
    const double source = nitsToPq(sourcePeakNits);
    const double maxLum = nitsToPq(targetPeakNits) / source;
    return pqToNits(float(std::max(1.5 * maxLum - 0.5, 0.0) * source));
}

namespace {
constexpr double kBt709ToBt2020[9] = {0.6274039, 0.3292830, 0.0433131, // row-major (ITU-R BT.2087)
                                      0.0690973, 0.9195404, 0.0113623,
                                      0.0163914, 0.0880133, 0.8955953};
} // namespace

void encodeOutput(OutputEncoding encoding, float *rgb)
{
    switch (encoding) {
    case OutputEncoding::ScRgb:
        break; // negative components keep colours outside BT.709
    case OutputEncoding::Pq: {
        const float r = rgb[0], g = rgb[1], b = rgb[2];
        for (int c = 0; c < 3; ++c) {
            const double *m = kBt709ToBt2020 + 3 * c;
            rgb[c] = nitsToPq(float(std::max(m[0] * r + m[1] * g + m[2] * b, 0.0)));
        }
        break;
    }
    case OutputEncoding::Sdr:
        for (int c = 0; c < 3; ++c)
            rgb[c] = linearToSrgb(std::clamp(rgb[c], 0.0f, 1.0f));
        break;
    }
}

void applyOutputStage(const OutputStage &s, float *rgba)
{
    const float alpha = rgba[3];
    float rgb[3];
    for (int c = 0; c < 3; ++c)
        rgb[c] = (alpha > 0.0f ? rgba[c] / alpha : 0.0f) * s.exposure * s.scale;

    bool altered;
    if (s.sourcePeak > s.peak) {
        // Hue-preserving: the curve acts on max(R,G,B) and all components follow its ratio.
        const float m = std::max({rgb[0], rgb[1], rgb[2]});
        const float sourceNits = s.sourcePeak * s.nitsPerUnit, targetNits = s.peak * s.nitsPerUnit;
        altered = m * s.nitsPerUnit > eetfKneeNits(sourceNits, targetNits);
        if (altered) {
            const float ratio = eetfBt2390(m * s.nitsPerUnit, sourceNits, targetNits) / (m * s.nitsPerUnit);
            for (float &v : rgb)
                v *= ratio;
        }
    } else {
        altered = std::max({rgb[0], rgb[1], rgb[2]}) > s.peak;
    }
    for (float &v : rgb)
        v = std::min(v, s.peak);
    if (s.clipWarning && altered) {
        rgb[0] = rgb[2] = s.peak;
        rgb[1] = 0.0f;
    }

    // Translucent pixels composite over the background in linear light, whatever the encoding.
    for (int c = 0; c < 3; ++c)
        rgb[c] = rgb[c] * alpha + s.background[c] * (1.0f - alpha);
    encodeOutput(s.encoding, rgb);
    for (int c = 0; c < 3; ++c)
        rgba[c] = rgb[c];
    rgba[3] = 1.0f;
}

Matrix3 rgbToXyz(const Chromaticities &c)
{
    const auto r = xyToXyz(c.r), g = xyToXyz(c.g), b = xyToXyz(c.b), w = xyToXyz(c.w);
    const Matrix3 m = {r[0], g[0], b[0], r[1], g[1], b[1], r[2], g[2], b[2]};
    const Matrix3 inv = inverse(m);
    const double s[3] = {inv[0] * w[0] + inv[1] * w[1] + inv[2] * w[2],
                         inv[3] * w[0] + inv[4] * w[1] + inv[5] * w[2],
                         inv[6] * w[0] + inv[7] * w[1] + inv[8] * w[2]};
    return {m[0] * s[0], m[1] * s[1], m[2] * s[2],
            m[3] * s[0], m[4] * s[1], m[5] * s[2],
            m[6] * s[0], m[7] * s[1], m[8] * s[2]};
}

bool isUsable(const Chromaticities &c)
{
    for (const double *xy : {c.r, c.g, c.b, c.w})
        if (!std::isfinite(xy[0]) || !std::isfinite(xy[1]) || std::abs(xy[1]) < 1e-6)
            return false;
    if (c.w[1] <= 0)
        return false;
    const Matrix3 m = rgbToRgb(c, kBt709);
    return std::all_of(m.begin(), m.end(), [](double v) { return std::isfinite(v) && std::abs(v) < 1e6; });
}

Matrix3 rgbToRgb(const Chromaticities &from, const Chromaticities &to)
{
    const Matrix3 toXyz = rgbToXyz(from), fromXyz = inverse(rgbToXyz(to));
    if (sameWhite(from, to))
        return multiply(fromXyz, toXyz);
    return multiply(fromXyz, multiply(bradford(xyToXyz(from.w), xyToXyz(to.w)), toXyz));
}

namespace {

// Fills `lut` (3 x 65536) so that entry c * 65536 + i holds f(c, i / 65535).
template <typename F>
std::vector<float> buildLuts(F f)
{
    std::vector<float> lut(3 * 65536);
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 65536; ++i)
            lut[std::size_t(c) * 65536 + std::size_t(i)] = f(c, float(i) / 65535.0f);
    return lut;
}

} // namespace

Converter::Converter(const Descriptor &d, int integerSourceBits)
{
    if (d.source == Descriptor::Source::Icc) {
        if (!initMatrixShaper(d.icc, integerSourceBits))
            m_iccTransform = createIccTransform(d.icc, &m_error);
        return;
    }
    m_transfer = d.transfer;
    m_gamma = d.gamma;
    m_convertPrimaries = !samePrimaries(d.primaries, kBt709);
    const Matrix3 toScRgb = rgbToRgb(d.primaries, kBt709);
    for (int i = 0; i < 9; ++i)
        m_matrix[i] = float(toScRgb[i]);
    const Matrix3 xyz = rgbToXyz(d.primaries);
    m_lumaWeights[0] = float(xyz[3]);
    m_lumaWeights[1] = float(xyz[4]);
    m_lumaWeights[2] = float(xyz[5]);

    // Integer sources hold exact multiples of 1/(2^n - 1): a 16-bit table is exact,
    // and it is also where narrow-range codes (16..235 at 8 bits) are expanded.
    if (integerSourceBits > 0 && integerSourceBits <= 16 && (m_transfer != Transfer::Linear || !d.fullRange)) {
        const double maxCode = double((1 << integerSourceBits) - 1), k = double(1 << (integerSourceBits - 8));
        const bool narrow = !d.fullRange;
        m_lut = buildLuts([&](int, float v) {
            const float signal = narrow ? float((v * maxCode - 16.0 * k) / (219.0 * k)) : v;
            return decodeChannel(m_transfer, m_gamma, signal);
        });
    }
}

// ICC profiles made of three tone curves and a 3x3 matrix (or one gray curve) are
// evaluated directly: LittleCMS's float pipeline for them is several times slower,
// and it clips tabulated curves at 1.0 anyway. The result is the same relative
// colorimetric transform: source colorants (already in the D50 PCS) to XYZ, then to
// linear BT.709 whose colorants are Bradford-adapted to D50, as LittleCMS does for
// the output profile. Profiles with a non-zero black point keep LittleCMS, since
// black-point compensation would change their shadows.
bool Converter::initMatrixShaper(const QByteArray &icc, int integerSourceBits)
{
    cmsHPROFILE profile = cmsOpenProfileFromMem(icc.constData(), cmsUInt32Number(icc.size()));
    if (!profile)
        return false; // the general path reports the error
    struct Close {
        cmsHPROFILE p;
        ~Close() { cmsCloseProfile(p); }
    } close{profile};
    if (qEnvironmentVariableIsSet("IMAGEVIEWER_ICC_LCMS")) // diagnostics: force LittleCMS
        return false;

    std::array<cmsToneCurve *, 3> curves{};
    Matrix3 sourceToXyz{};
    const cmsColorSpaceSignature space = cmsGetColorSpace(profile);
    if (space == cmsSigRgbData && cmsIsMatrixShaper(profile)) {
        const auto *r = static_cast<const cmsCIEXYZ *>(cmsReadTag(profile, cmsSigRedColorantTag));
        const auto *g = static_cast<const cmsCIEXYZ *>(cmsReadTag(profile, cmsSigGreenColorantTag));
        const auto *b = static_cast<const cmsCIEXYZ *>(cmsReadTag(profile, cmsSigBlueColorantTag));
        curves = {static_cast<cmsToneCurve *>(cmsReadTag(profile, cmsSigRedTRCTag)),
                  static_cast<cmsToneCurve *>(cmsReadTag(profile, cmsSigGreenTRCTag)),
                  static_cast<cmsToneCurve *>(cmsReadTag(profile, cmsSigBlueTRCTag))};
        if (!r || !g || !b || !curves[0] || !curves[1] || !curves[2])
            return false;
        sourceToXyz = {r->X, g->X, b->X, r->Y, g->Y, b->Y, r->Z, g->Z, b->Z};
    } else if (space == cmsSigGrayData && cmsIsMatrixShaper(profile)) {
        auto *gray = static_cast<cmsToneCurve *>(cmsReadTag(profile, cmsSigGrayTRCTag));
        if (!gray)
            return false;
        curves = {gray, gray, gray}; // gray maps to R = G = B at the white point
    } else {
        return false;
    }
    cmsCIEXYZ black{};
    if (cmsDetectBlackPoint(&black, profile, INTENT_RELATIVE_COLORIMETRIC, 0) && black.Y > 1e-5)
        return false;

    if (space == cmsSigRgbData) {
        const Matrix3 bt709ToD50 = multiply(bradford(xyToXyz(kBt709.w), kD50), rgbToXyz(kBt709));
        const Matrix3 m = multiply(inverse(bt709ToD50), sourceToXyz);
        for (int i = 0; i < 9; ++i)
            m_matrix[i] = float(m[i]);
        m_convertPrimaries = true;
    }
    if (integerSourceBits > 0 && integerSourceBits <= 16) {
        m_lut = buildLuts([&](int c, float v) { return cmsEvalToneCurveFloat(curves[std::size_t(c)], v); });
    } else {
        for (std::size_t c = 0; c < 3; ++c)
            m_curves[c] = cmsDupToneCurve(curves[c]); // the profile is closed below
    }
    m_transfer = Transfer::Linear; // curves replace the analytic transfer
    return true;
}

Converter::~Converter()
{
    if (m_iccTransform)
        cmsDeleteTransform(static_cast<cmsHTRANSFORM>(m_iccTransform));
    for (void *curve : m_curves)
        if (curve)
            cmsFreeToneCurve(static_cast<cmsToneCurve *>(curve));
}

float Converter::decode(int channel, float v) const
{
    if (const void *curve = m_curves[std::size_t(channel)])
        return cmsEvalToneCurveFloat(static_cast<const cmsToneCurve *>(curve), v);
    return decodeChannel(m_transfer, m_gamma, v);
}

void Converter::apply(float *rgba, std::size_t pixelCount) const
{
    if (!m_error.isEmpty())
        return;
    float *const end = rgba + pixelCount * 4;
    if (m_iccTransform) {
        std::vector<float> rgb(pixelCount * 3);
        cmsDoTransform(static_cast<cmsHTRANSFORM>(m_iccTransform), rgba, rgb.data(), cmsUInt32Number(pixelCount));
        const float *q = rgb.data();
        for (float *p = rgba; p < end; p += 4, q += 3) {
            p[0] = q[0];
            p[1] = q[1];
            p[2] = q[2];
        }
        return;
    }

    if (!m_lut.empty()) {
        const float *lut = m_lut.data();
        for (float *p = rgba; p < end; p += 4)
            for (int c = 0; c < 3; ++c)
                p[c] = lut[std::size_t(c) * 65536 + std::size_t(std::lrint(std::clamp(p[c], 0.0f, 1.0f) * 65535.0f))];
    } else if (m_transfer != Transfer::Linear || m_curves[0]) {
        for (float *p = rgba; p < end; p += 4)
            for (int c = 0; c < 3; ++c)
                p[c] = decode(c, p[c]);
    }

    if (m_transfer == Transfer::Hlg) {
        // BT.2100 HLG OOTF for the nominal 1000 cd/m2 display (gamma 1.2): scene light
        // to display light, then scaled so that 203 cd/m2 is 1.0.
        const float peak = kHlgNominalPeakNits;
        const float gamma = 1.2f + 0.42f * std::log10(peak / 1000.0f);
        for (float *p = rgba; p < end; p += 4) {
            const float ys = m_lumaWeights[0] * p[0] + m_lumaWeights[1] * p[1] + m_lumaWeights[2] * p[2];
            const float k = ys > 0 ? peak * std::pow(ys, gamma - 1.0f) / kSdrReferenceWhiteNits : 0.0f;
            p[0] *= k;
            p[1] *= k;
            p[2] *= k;
        }
    }

    if (m_convertPrimaries) {
        const float *m = m_matrix;
        for (float *p = rgba; p < end; p += 4) {
            const float r = p[0], g = p[1], b = p[2];
            p[0] = m[0] * r + m[1] * g + m[2] * b;
            p[1] = m[3] * r + m[4] * g + m[5] * b;
            p[2] = m[6] * r + m[7] * g + m[8] * b;
        }
    }
}

} // namespace color
