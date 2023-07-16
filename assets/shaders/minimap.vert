#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "minimap.glsl"
#include "ui.glsl"

const u32   c_vertexCount                  = 6;
const f32v2 c_unitPositions[c_vertexCount] = {
    f32v2(0, 1),
    f32v2(1, 1),
    f32v2(0, 0),
    f32v2(1, 1),
    f32v2(1, 0),
    f32v2(0, 0),
};

bind_draw_data(0) readonly uniform Draw { MinimapData u_draw; };

bind_internal(0) out f32v2 out_texcoord;

void main() {
  const f32v2 pos  = u_draw.data1.xy;
  const f32v2 size = u_draw.data1.zw;

  const f32v2 uiPos = pos + c_unitPositions[in_vertexIndex] * size;

  out_vertexPosition = ui_norm_to_ndc(uiPos);
  out_texcoord       = c_unitPositions[in_vertexIndex];
}
