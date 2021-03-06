#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_internal(0) in f32v3 out_worldGridPos;
bind_internal(1) in flat f32 in_gridHalfSize;
bind_internal(2) in flat f32v4 in_color;
bind_internal(3) in flat f32 in_fadeFraction;

bind_internal(0) out f32v4 out_color;

f32 compute_fade(const f32v3 center) {
  const f32 dist = length(out_worldGridPos - center);
  return 1.0 - smoothstep(in_gridHalfSize * (1.0 - in_fadeFraction), in_gridHalfSize, dist);
}

void main() {
  const f32v3 camPos = u_global.camPosition.xyz;
  out_color          = f32v4(in_color.rgb, in_color.a * compute_fade(camPos));
}
