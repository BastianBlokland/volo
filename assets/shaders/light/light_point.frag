#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "texture.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_global(2) uniform sampler2D u_texGeoNormalTags;
bind_global(3) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_position;
bind_internal(1) in flat f32v3 in_radiance;
bind_internal(2) in flat f32v3 in_attenuation;

bind_internal(0) out f32v4 out_color;

f32v3 clip_to_world(const f32v3 clipPos) {
  const f32v4 v = u_global.viewProjInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

void main() {
  const f32v2 texcoord   = in_fragCoord.xy / u_global.resolution.xy;
  const f32v4 colorRough = texture(u_texGeoColorRough, texcoord);
  const f32v4 normalTags = texture(u_texGeoNormalTags, texcoord);
  const f32   depth      = texture(u_texGeoDepth, texcoord).r;

  const f32v3 clipPos  = f32v3(texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world(clipPos);
  const f32v3 viewDir  = normalize(u_global.camPosition.xyz - worldPos);

  PbrSurface surf;
  surf.position     = worldPos;
  surf.color        = colorRough.rgb;
  surf.normal       = normal_tex_decode(normalTags.xyz);
  surf.roughness    = colorRough.a;
  surf.metallicness = 0.0; // TODO: Support metals.

  out_color = f32v4(pbr_light_point(in_radiance, in_position, in_attenuation, viewDir, surf), 1.0);
}
