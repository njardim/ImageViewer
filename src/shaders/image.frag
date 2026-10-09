#version 440

// Output stage of the colour pipeline (docs/PLAN.md §6.2). Image textures hold
// linear scRGB (BT.709 primaries, 1.0 = SDR reference white), premultiplied.
// Overlay textures hold sRGB-encoded UI pixels, premultiplied.
// color::applyOutputStage() (src/color.cpp) is the CPU reference of the image path.
// Overlay quads draw without blending: they work out the image underneath again (same texture,
// same coordinates) and lay every overlay up to their own over it as SDR does (D-49).

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Params {
    mat4 clipCorrection;
    vec4 adjust; // x: exposure multiplier, y: output units per working unit, z: output peak, w: unused
    vec4 tone;   // x: nits per output unit, y: content peak (output units; <= peak: clip, no tone mapping),
                 // z: PQ(content peak in nits), w: PQ(output peak) / PQ(content peak)
    vec4 background; // rgb: what translucent image pixels composite over (linear, output units)
    vec4 checker;    // xy: checkerboard cells across the texture; z: 1 = on (image layer only)
    vec4 checkerColour; // rgb: colour of every other cell (linear, output units)
    ivec4 modes; // x: 0 SDR sRGB, 1 linear scRGB/EDR, 2 PQ BT.2020; y: unused; z: clip warning;
                 // w: 1 = framebuffer rows count from the bottom (OpenGL)
    vec4 outside;   // rgb: the window background where there is no image (linear, output units)
    vec4 imageRect; // the image on screen, device pixels: x, y, width, height (width 0: none)
    vec4 uvMap;     // xy: texture coordinates at imageRect's top-left; zw: their change per pixel along x
    vec4 uvMapY;    // xy: their change per pixel along y; z: target height in pixels
    vec4 layers[4]; // overlay rectangles, device pixels (width 0: absent), in drawing order
};

layout(binding = 1) uniform sampler2D tex; // the image
layout(binding = 2) uniform sampler2D overlay0;
layout(binding = 3) uniform sampler2D overlay1;
layout(binding = 4) uniform sampler2D overlay2;
layout(binding = 5) uniform sampler2D overlay3;

const int MODE_SDR = 0;
const int MODE_SCRGB = 1;
const int MODE_PQ = 2;

const float PQ_M1 = 2610.0 / 16384.0;
const float PQ_M2 = 2523.0 / 4096.0 * 128.0;
const float PQ_C1 = 3424.0 / 4096.0;
const float PQ_C2 = 2413.0 / 4096.0 * 32.0;
const float PQ_C3 = 2392.0 / 4096.0 * 32.0;

float srgbEncode(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float srgbDecode(float x)
{
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

// The sRGB curve on both sides of [0, 1] (HDR content and negative scRGB under a panel).
vec3 srgbEncodeExtended(vec3 x)
{
    vec3 a = abs(x);
    return sign(x) * mix(1.055 * pow(a, vec3(1.0 / 2.4)) - 0.055, 12.92 * a, vec3(lessThanEqual(a, vec3(0.0031308))));
}

vec3 srgbDecodeExtended(vec3 x)
{
    vec3 a = abs(x);
    return sign(x) * mix(pow((a + 0.055) / 1.055, vec3(2.4)), a / 12.92, vec3(lessThanEqual(a, vec3(0.04045))));
}

vec3 pqEncode(vec3 nits)
{
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(PQ_M1));
    return pow((PQ_C1 + PQ_C2 * y) / (1.0 + PQ_C3 * y), vec3(PQ_M2));
}

float pqDecode(float e)
{
    float p = pow(clamp(e, 0.0, 1.0), 1.0 / PQ_M2);
    return 10000.0 * pow(max(p - PQ_C1, 0.0) / (PQ_C2 - PQ_C3 * p), 1.0 / PQ_M1);
}

// Linear BT.709 -> linear BT.2020 (ITU-R BT.2087), column-major, and back.
const mat3 BT709_TO_BT2020 = mat3(0.6274039, 0.0690973, 0.0163914,
                                  0.3292830, 0.9195404, 0.0880133,
                                  0.0433131, 0.0113623, 0.8955953);
const mat3 BT2020_TO_BT709 = mat3(1.6604910, -0.1245505, -0.0181507,
                                  -0.5876411, 1.1328999, -0.1005789,
                                  -0.0728499, -0.0083494, 1.1187296);

// What the image shows at texture coordinates `uv`: linear, output units, before the encoding.
vec3 displayed(vec2 uv)
{
    vec4 texel = texture(tex, uv);
    float alpha = texel.a;
    vec3 rgb = alpha > 0.0 ? texel.rgb / alpha : vec3(0.0);
    rgb *= adjust.x * adjust.y;

    float peak = adjust.z;
    bool altered;
    if (tone.y > peak) {
        // ITU-R BT.2390 EETF (zero black levels) on max(R,G,B), hue preserved by scaling all components.
        float m = max(rgb.r, max(rgb.g, rgb.b));
        float e1 = pqEncode(vec3(m * tone.x)).x / tone.z;
        float knee = max(1.5 * tone.w - 0.5, 0.0);
        altered = e1 > knee;
        if (altered) {
            float t = min((e1 - knee) / (1.0 - knee), 1.0);
            float t2 = t * t;
            float t3 = t2 * t;
            float e2 = (2.0 * t3 - 3.0 * t2 + 1.0) * knee + (t3 - 2.0 * t2 + t) * (1.0 - knee)
                     + (-2.0 * t3 + 3.0 * t2) * tone.w;
            rgb *= pqDecode(e2 * tone.z) / (m * tone.x);
        }
    }
    // The peak limits the display: in BT.2020 for scRGB/EDR and PQ, whose gamuts are wider
    // than BT.709, so that wide-gamut colours below the peak are not cut (color.cpp).
    bool wide = modes.x == MODE_SCRGB || modes.x == MODE_PQ;
    if (wide)
        rgb = BT709_TO_BT2020 * rgb;
    if (tone.y <= peak)
        altered = any(greaterThan(rgb, vec3(peak)));
    rgb = min(rgb, vec3(peak));
    if (wide)
        rgb = BT2020_TO_BT709 * rgb;
    if (modes.z != 0 && altered)
        rgb = vec3(peak, 0.0, peak);

    // Composite in linear light, so alpha means the same in every output encoding (F6).
    // The checkerboard only chooses what lies underneath; color::applyOutputStage()
    // composites over the plain background, which is what the harness checks.
    vec3 under = background.rgb;
    if (checker.z > 0.5) {
        vec2 cell = floor(uv * checker.xy); // float maths: legacy GLSL targets lack integer '&'
        if (mod(cell.x + cell.y, 2.0) >= 1.0)
            under = checkerColour.rgb;
    }
    return rgb * alpha + under * (1.0 - alpha);
}

vec3 encodeOutput(vec3 rgb)
{
    if (modes.x == MODE_SCRGB)
        return rgb; // negative components keep colours outside BT.709
    if (modes.x == MODE_PQ)
        return pqEncode(max(BT709_TO_BT2020 * rgb, vec3(0.0)));
    return vec3(srgbEncode(rgb.r), srgbEncode(rgb.g), srgbEncode(rgb.b));
}

bool within(vec2 p, vec4 r)
{
    // Pixel centres on the top or left edge are in, on the bottom or right edge out, as the
    // rasteriser decides for the image quad.
    return r.z > 0.0 && p.x >= r.x && p.x < r.x + r.z && p.y >= r.y && p.y < r.y + r.w;
}

// A UI pixel over `under` as SDR shows it (D-49): blended in sRGB-encoded values relative to
// SDR white, whatever the output; brighter content underneath follows the same curve.
vec3 overUi(vec3 under, vec4 ui, bool present)
{
    if (!present)
        return under;
    vec3 encoded = ui.rgb + srgbEncodeExtended(under / adjust.y) * (1.0 - ui.a);
    return srgbDecodeExtended(encoded) * adjust.y;
}

void main()
{
    if (v_texcoord.x > -0.5) { // the image quad: coordinates in [0, 1]
        fragColor = vec4(encodeOutput(displayed(v_texcoord)), 1.0);
        return;
    }
    // Overlay quad number `layer` (its texture coordinates carry -1 - layer).
    int layer = int(-v_texcoord.x + 0.5) - 1;
    vec2 p = vec2(gl_FragCoord.x, modes.w != 0 ? uvMapY.z - gl_FragCoord.y : gl_FragCoord.y);
    // Every texture is read on every pixel of the quad (the same branch for all of them, so the
    // image's mipmaps get well-defined derivatives); the rectangles choose what counts.
    vec2 d = p - imageRect.xy;
    vec3 rgb = displayed(uvMap.xy + d.x * uvMap.zw + d.y * uvMapY.xy);
    if (!within(p, imageRect))
        rgb = outside.rgb;
    vec4 ui0 = texture(overlay0, (p - layers[0].xy) / max(layers[0].zw, vec2(1.0)));
    vec4 ui1 = texture(overlay1, (p - layers[1].xy) / max(layers[1].zw, vec2(1.0)));
    vec4 ui2 = texture(overlay2, (p - layers[2].xy) / max(layers[2].zw, vec2(1.0)));
    vec4 ui3 = texture(overlay3, (p - layers[3].xy) / max(layers[3].zw, vec2(1.0)));
    rgb = overUi(rgb, ui0, layer >= 0 && within(p, layers[0]));
    rgb = overUi(rgb, ui1, layer >= 1 && within(p, layers[1]));
    rgb = overUi(rgb, ui2, layer >= 2 && within(p, layers[2]));
    rgb = overUi(rgb, ui3, layer >= 3 && within(p, layers[3]));
    fragColor = vec4(encodeOutput(rgb), 1.0);
}
