#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "tags.glsl"
#include "texture.glsl"

struct ShadeBaseData {
  f32v4 sunRadiance; // rgb: sunRadiance, a: unused.
  f32v4 sunDir;      // xyz: sunDir, a: unused.
  f32   ambient;
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_global(2) uniform sampler2D u_texGeoNormalTags;
bind_global(3) uniform sampler2D u_texGeoDepth;
bind_draw_data(0) readonly uniform Draw { ShadeBaseData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 clip_to_world(const f32v3 clipPos) {
  const f32v4 v = u_global.viewProjInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

void main() {
  const f32v4 colorRough = texture(u_texGeoColorRough, in_texcoord);
  const f32v4 normalTags = texture(u_texGeoNormalTags, in_texcoord);
  const f32   depth      = texture(u_texGeoDepth, in_texcoord).r;

  PbrSurface surf;
  surf.color        = colorRough.rgb;
  surf.normal       = normalTags.xyz;
  surf.roughness    = colorRough.a;
  surf.metallicness = 0.0; // TODO: Support metals.

  const u32   tags     = tags_tex_decode(normalTags.w);
  const f32v3 clipPos  = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world(clipPos);
  const f32v3 viewDir  = normalize(u_global.camPosition.xyz - worldPos);

  const f32v3 ambient  = surf.color * u_draw.ambient;
  const f32v3 sunLight = pbr_light_dir(u_draw.sunRadiance.rgb, u_draw.sunDir.xyz, viewDir, surf);

  out_color = f32v4(ambient + sunLight, 1.0);

  // TODO: Apply these effects at a later stage (after all the lighting has been done).
  if (tag_is_set(tags, tag_selected_bit)) {
    out_color.rgb += (1.0 - abs(dot(surf.normal, viewDir)));
  }
  if (tag_is_set(tags, tag_damaged_bit)) {
    out_color.rgb = mix(out_color.rgb, f32v3(0.8, 0.1, 0.1), abs(dot(surf.normal, viewDir)));
  }
}
