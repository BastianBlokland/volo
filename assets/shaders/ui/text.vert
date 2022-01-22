#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "ui.glsl"

// Unit quad.
const f32_vec2 c_positions[] = {
    f32_vec2(0, 1),
    f32_vec2(1, 1),
    f32_vec2(0, 0),
    f32_vec2(1, 1),
    f32_vec2(1, 0),
    f32_vec2(0, 0),
};

const f32_vec2 c_texCoords[] = {
    f32_vec2(0, 1),
    f32_vec2(1, 1),
    f32_vec2(0, 0),
    f32_vec2(1, 1),
    f32_vec2(1, 0),
    f32_vec2(0, 0),
};

bind_internal(0) out f32_vec2 out_texcoord;

void main() {
  const f32_vec2 uiPos = c_positions[in_vertexIndex];
  out_vertexPosition   = ui_to_ndc(uiPos);
  out_texcoord         = c_texCoords[in_vertexIndex];
}
