#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "texture.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(3) uniform sampler2D u_texGeoDepth;

bind_internal(0) in f32v3 in_position;
bind_internal(1) in f32v3 in_radiance;
bind_internal(2) in f32v3 in_attenuation;

bind_internal(0) out f32v4 out_color;

f32v3 clip_to_world(const f32v3 clipPos) {
  const f32v4 v = u_global.viewProjInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

void main() {
  const f32v2 texcoord = in_fragCoord.xy / u_global.resolution.xy;
  const f32   depth    = texture(u_texGeoDepth, texcoord).r;

  const f32v3 clipPos  = f32v3(texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world(clipPos);
  const f32   dist     = length(worldPos - in_position);

  const f32 frac = pbr_attenuation_resolve(in_attenuation, dist);
  out_color      = mix(f32v4(0, 0, 0, 1), f32v4(normalize(in_radiance), 1), frac);
}
