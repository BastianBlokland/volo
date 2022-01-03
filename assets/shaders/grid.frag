#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/global.glsl"

const i32 c_distFadeBegin = 50;
const i32 c_distFadeEnd   = 100;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_internal(0) in f32_vec3 in_gridPos;
bind_internal(1) in flat f32_vec4 in_color;

bind_internal(0) out f32_vec4 out_color;

f32 compute_fade(const f32_vec3 center) {
  const f32 dist = length(in_gridPos - center);
  return 1.0 - smoothstep(c_distFadeBegin, c_distFadeEnd, dist);
}

void main() {
  const f32_vec3 camPos = u_global.camPosition.xyz;
  out_color             = f32_vec4(in_color.rgb, in_color.a * compute_fade(camPos));
}
