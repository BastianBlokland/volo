#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

// Fullscreen at zero depth.
const f32_vec4 c_positions[] = {
    f32_vec4(-1, 1, 1, 1),
    f32_vec4(1, 1, 1, 1),
    f32_vec4(1, -1, 1, 1),
    f32_vec4(-1, -1, 1, 1),
};

const f32_vec2 c_texCoords[] = {
    f32_vec2(0, 0),
    f32_vec2(1, 0),
    f32_vec2(1, 1),
    f32_vec2(0, 1),
};

bind_internal(0) out f32_vec2 out_texcoord;

void main() {
  out_vertexPosition = c_positions[in_vertexIndex];
  out_texcoord       = c_texCoords[in_vertexIndex];
}
