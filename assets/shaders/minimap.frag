#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "minimap.glsl"

bind_draw_data(0) readonly uniform Draw { MinimapData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32 alpha = u_draw.data2.x;

  out_color = f32v4(in_texcoord, 0, alpha);
}
