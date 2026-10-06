#include "color.h"

#include <lcms2.h>

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

// Relative linear value (1.0 = SDR reference white) for a single channel.
float decodeChannel(Transfer t, float v)
{
    switch (t) {
    case Transfer::Linear: return v;
    case Transfer::Srgb: return srgbToLinear(v);
    case Transfer::Bt1886: return bt1886ToLinear(v);
    case Transfer::Gamma18: return v < 0 ? -std::pow(-v, 1.8f) : std::pow(v, 1.8f);
    case Transfer::Gamma22: return v < 0 ? -std::pow(-v, 2.2f) : std::pow(v, 2.2f);
    case Transfer::Gamma28: return v < 0 ? -std::pow(-v, 2.8f) : std::pow(v, 2.8f);
    case Transfer::Pq: return pqToNits(v) / kSdrReferenceWhiteNits;
    case Transfer::Hlg: break; // needs all three channels, handled separately
    }
    return v;
}

float hlgInverseOetf(float v)
{
    constexpr float a = 0.17883277f, b = 0.28466892f, c = 0.55991073f;
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.5f ? v * v / 3.0f : (std::exp((v - c) / a) + b) / 12.0f;
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

bool transferFromCicp(int code, Transfer *out)
{
    switch (code) {
    case 1:
    case 6:
    case 14:
    case 15: *out = Transfer::Bt1886; return true;
    case 4: *out = Transfer::Gamma22; return true;
    case 5: *out = Transfer::Gamma28; return true;
    case 8: *out = Transfer::Linear; return true;
    case 13: *out = Transfer::Srgb; return true;
    case 16: *out = Transfer::Pq; return true;
    case 18: *out = Transfer::Hlg; return true;
    default: return false;
    }
}

QString transferName(Transfer t)
{
    switch (t) {
    case Transfer::Linear: return QStringLiteral("linear");
    case Transfer::Srgb: return QStringLiteral("sRGB");
    case Transfer::Bt1886: return QStringLiteral("BT.1886 (γ2.4)");
    case Transfer::Gamma18: return QStringLiteral("γ1.8");
    case Transfer::Gamma22: return QStringLiteral("γ2.2");
    case Transfer::Gamma28: return QStringLiteral("γ2.8");
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
    const cmsUInt32Number n = cmsGetProfileInfo(profile, cmsInfoDescription, "en", "US", buffer, 255);
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

Matrix3 rgbToRgb(const Chromaticities &from, const Chromaticities &to)
{
    Matrix3 m = multiply(inverse(rgbToXyz(to)), rgbToXyz(from));
    if (sameWhite(from, to))
        return m;
    // Bradford chromatic adaptation from the source white to the destination white.
    const Matrix3 bradford = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    const auto ws = xyToXyz(from.w), wd = xyToXyz(to.w);
    auto cone = [&](const std::array<double, 3> &w) {
        return std::array<double, 3>{bradford[0] * w[0] + bradford[1] * w[1] + bradford[2] * w[2],
                                     bradford[3] * w[0] + bradford[4] * w[1] + bradford[5] * w[2],
                                     bradford[6] * w[0] + bradford[7] * w[1] + bradford[8] * w[2]};
    };
    const auto cs = cone(ws), cd = cone(wd);
    const Matrix3 scale = {cd[0] / cs[0], 0, 0, 0, cd[1] / cs[1], 0, 0, 0, cd[2] / cs[2]};
    const Matrix3 adapt = multiply(inverse(bradford), multiply(scale, bradford));
    return multiply(inverse(rgbToXyz(to)), multiply(adapt, rgbToXyz(from)));
}

Converter::Converter(const Descriptor &d, int integerSourceBits)
{
    if (d.source == Descriptor::Source::Icc) {
        m_iccTransform = createIccTransform(d.icc, &m_error);
        return;
    }
    m_transfer = d.transfer;
    m_convertPrimaries = !samePrimaries(d.primaries, kBt709);
    const Matrix3 toScRgb = rgbToRgb(d.primaries, kBt709);
    for (int i = 0; i < 9; ++i)
        m_matrix[i] = float(toScRgb[i]);
    const Matrix3 xyz = rgbToXyz(d.primaries);
    m_lumaWeights[0] = float(xyz[3]);
    m_lumaWeights[1] = float(xyz[4]);
    m_lumaWeights[2] = float(xyz[5]);
    // Integer sources hold exact multiples of 1/(2^n - 1): a 16-bit table is exact.
    if (integerSourceBits > 0 && integerSourceBits <= 16 && m_transfer != Transfer::Linear
        && m_transfer != Transfer::Hlg) {
        m_lut.resize(65536);
        for (int i = 0; i < 65536; ++i)
            m_lut[std::size_t(i)] = decodeChannel(m_transfer, float(i) / 65535.0f);
    }
}

Converter::~Converter()
{
    if (m_iccTransform)
        cmsDeleteTransform(static_cast<cmsHTRANSFORM>(m_iccTransform));
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

    if (m_transfer == Transfer::Hlg) {
        // BT.2100 HLG: inverse OETF, then the OOTF for the nominal 1000 cd/m2 display.
        const float peak = kHlgNominalPeakNits;
        const float gamma = 1.2f + 0.42f * std::log10(peak / 1000.0f);
        for (float *p = rgba; p < end; p += 4) {
            const float r = hlgInverseOetf(p[0]), g = hlgInverseOetf(p[1]), b = hlgInverseOetf(p[2]);
            const float ys = m_lumaWeights[0] * r + m_lumaWeights[1] * g + m_lumaWeights[2] * b;
            const float k = ys > 0 ? peak * std::pow(ys, gamma - 1.0f) / kSdrReferenceWhiteNits : 0.0f;
            p[0] = r * k;
            p[1] = g * k;
            p[2] = b * k;
        }
    } else if (!m_lut.empty()) {
        for (float *p = rgba; p < end; p += 4)
            for (int c = 0; c < 3; ++c)
                p[c] = m_lut[std::size_t(std::lrint(std::clamp(p[c], 0.0f, 1.0f) * 65535.0f))];
    } else if (m_transfer != Transfer::Linear) {
        for (float *p = rgba; p < end; p += 4)
            for (int c = 0; c < 3; ++c)
                p[c] = decodeChannel(m_transfer, p[c]);
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
