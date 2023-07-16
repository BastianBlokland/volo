#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "ui.glsl"

const u32   c_vertexCount                  = 6;
const f32v2 c_unitPositions[c_vertexCount] = {
    f32v2(-0.5, +0.5),
    f32v2(+0.5, +0.5),
    f32v2(-0.5, -0.5),
    f32v2(+0.5, +0.5),
    f32v2(+0.5, -0.5),
    f32v2(-0.5, -0.5),
};
const f32v2 c_unitTexCoords[c_vertexCount] = {
    f32v2(0, 1),
    f32v2(1, 1),
    f32v2(0, 0),
    f32v2(1, 1),
    f32v2(1, 0),
    f32v2(0, 0),
};

bind_internal(0) out f32v2 out_texcoord;

void main() {
  const f32v2 unitPos = c_unitPositions[in_vertexIndex];

  out_vertexPosition = ui_norm_to_ndc(unitPos);
  out_texcoord       = c_unitTexCoords[in_vertexIndex];
}
