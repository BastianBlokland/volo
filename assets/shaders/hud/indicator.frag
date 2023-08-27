#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

const f32 c_edgeSharpness = 4.0;

bind_internal(0) in flat f32v4 in_color;
bind_internal(1) in f32 in_radiusFrac;

bind_internal(0) out f32v4 out_color;

f32 edge_blend_alpha(const f32 radiusFrac) {
  return 1.0 - pow(abs(in_radiusFrac * 2.0 - 1.0), c_edgeSharpness);
}

void main() {
  const f32 edgeBlendAlpha = edge_blend_alpha(in_radiusFrac);
  out_color                = f32v4(in_color.rgb, in_color.a * edgeBlendAlpha);
}
