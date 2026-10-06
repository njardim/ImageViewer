#version 440

// Output stage of the colour pipeline (docs/PLANO.md §6.2). Image textures hold
// linear scRGB (BT.709 primaries, 1.0 = SDR reference white), premultiplied.
// Overlay textures hold sRGB-encoded UI pixels, premultiplied.

layout(location = 0) in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Params {
    mat4 clipCorrection;
    vec4 adjust; // x: exposure multiplier, y: output units per working unit, z: output peak, w: unused
    ivec4 modes; // x: 0 SDR sRGB, 1 linear scRGB/EDR, 2 PQ BT.2020; y: 0 image, 1 overlay; z: clip warning
};

layout(binding = 1) uniform sampler2D tex;

const int MODE_SDR = 0;
const int MODE_SCRGB = 1;
const int MODE_PQ = 2;

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
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * y) / (1.0 + c3 * y), vec3(m2));
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

    // Phase 0: hard clip at the output peak. BT.2390 EETF arrives in Phase 1.
    bool clipped = any(greaterThan(rgb, vec3(adjust.z)));
    rgb = min(rgb, vec3(adjust.z));
    if (modes.z != 0 && modes.y == 0 && clipped)
        rgb = vec3(adjust.z, 0.0, adjust.z);

    vec3 encoded;
    if (modes.x == MODE_SCRGB)
        encoded = rgb; // negative components keep colours outside BT.709
    else if (modes.x == MODE_PQ)
        encoded = pqEncode(max(BT709_TO_BT2020 * rgb, vec3(0.0)));
    else
        encoded = vec3(srgbEncode(rgb.r), srgbEncode(rgb.g), srgbEncode(rgb.b));

    fragColor = vec4(encoded * alpha, alpha);
}
