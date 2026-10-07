#version 440

// Output stage of the colour pipeline (docs/PLAN.md §6.2). Image textures hold
// linear scRGB (BT.709 primaries, 1.0 = SDR reference white), premultiplied.
// Overlay textures hold sRGB-encoded UI pixels, premultiplied.
// color::applyOutputStage() (src/color.cpp) is the CPU reference of this code.

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
    ivec4 modes; // x: 0 SDR sRGB, 1 linear scRGB/EDR, 2 PQ BT.2020; y: 0 image, 1 overlay; z: clip warning
};

layout(binding = 1) uniform sampler2D tex;

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

// Linear BT.709 -> linear BT.2020 (ITU-R BT.2087), column-major.
const mat3 BT709_TO_BT2020 = mat3(0.6274039, 0.0690973, 0.0163914,
                                  0.3292830, 0.9195404, 0.0880133,
                                  0.0433131, 0.0113623, 0.8955953);

void main()
{
    vec4 texel = texture(tex, v_texcoord);
    float alpha = texel.a;
    vec3 rgb = alpha > 0.0 ? texel.rgb / alpha : vec3(0.0);

    if (modes.y == 1)
        rgb = vec3(srgbDecode(rgb.r), srgbDecode(rgb.g), srgbDecode(rgb.b)); // UI sits at SDR white
    else
        rgb *= adjust.x;

    rgb *= adjust.y;

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
    } else {
        altered = any(greaterThan(rgb, vec3(peak)));
    }
    rgb = min(rgb, vec3(peak));
    if (modes.z != 0 && modes.y == 0 && altered)
        rgb = vec3(peak, 0.0, peak);

    if (modes.y == 0) {
        // Composite in linear light, so alpha means the same in every output encoding (F6).
        // The checkerboard only chooses what lies underneath; color::applyOutputStage()
        // composites over the plain background, which is what the harness checks.
        vec3 under = background.rgb;
        if (checker.z > 0.5) {
            vec2 cell = floor(v_texcoord * checker.xy); // float maths: legacy GLSL targets lack integer '&'
            if (mod(cell.x + cell.y, 2.0) >= 1.0)
                under = checkerColour.rgb;
        }
        rgb = rgb * alpha + under * (1.0 - alpha);
        alpha = 1.0;
    }

    vec3 encoded;
    if (modes.x == MODE_SCRGB)
        encoded = rgb; // negative components keep colours outside BT.709
    else if (modes.x == MODE_PQ)
        encoded = pqEncode(max(BT709_TO_BT2020 * rgb, vec3(0.0)));
    else
        encoded = vec3(srgbEncode(rgb.r), srgbEncode(rgb.g), srgbEncode(rgb.b));

    fragColor = vec4(encoded * alpha, alpha);
}
