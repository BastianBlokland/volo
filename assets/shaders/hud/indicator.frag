#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"

const f32 c_behindThreshold = 0.5;
const f32 c_behindAlphaMul  = 0.15;
const f32 c_edgeSharpness   = 4.0;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(2) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v4 in_color;
bind_internal(1) in f32 in_radiusFrac;

bind_internal(0) out f32v4 out_color;

f32 clip_to_view_depth(const f32v3 clipPos) {
  const f32v4 v = u_global.projInv * f32v4(clipPos, 1);
  return v.z / v.w;
}

f32 edge_blend_alpha(const f32 radiusFrac) {
  return 1.0 - pow(abs(in_radiusFrac * 2.0 - 1.0), c_edgeSharpness);
}

void main() {
  const f32v2 texcoord         = in_fragCoord.xy / u_global.resolution.xy;
  const f32v2 clipXY           = texcoord * 2.0 - 1.0;
  const f32   sceneDepth       = texture(u_texGeoDepth, texcoord).r;
  const f32   sceneLinearDepth = clip_to_view_depth(f32v3(clipXY, sceneDepth));
  const f32   fragLinearDepth  = clip_to_view_depth(f32v3(clipXY, in_fragCoord.z));

  f32 alpha = in_color.a;
  alpha *= edge_blend_alpha(in_radiusFrac);
  if ((fragLinearDepth - sceneLinearDepth) >= c_behindThreshold) {
    alpha *= c_behindAlphaMul;
  }
  out_color = f32v4(in_color.rgb, alpha);
}
