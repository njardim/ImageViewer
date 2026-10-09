#version 440

layout(location = 0) in vec2 position; // device pixels, origin top-left
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

// Must match image.frag and Renderer::Uniforms.
layout(std140, binding = 0) uniform Params {
    mat4 clipCorrection; // pixels -> clip space, includes QRhi::clipSpaceCorrMatrix()
    vec4 adjust;         // x: exposure multiplier, y: output scale, z: output peak, w: unused
    vec4 tone;           // tone mapping parameters, see image.frag
    vec4 background;     // see image.frag
    vec4 checker;        // see image.frag
    vec4 checkerColour;  // see image.frag
    ivec4 modes;         // see image.frag
    vec4 outside;        // see image.frag
    vec4 imageRect;
    vec4 uvMap;
    vec4 uvMapY;
    vec4 layers[4];
};

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    v_texcoord = texcoord;
    gl_Position = clipCorrection * vec4(position, 0.0, 1.0);
}
