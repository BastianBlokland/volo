#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "texture.glsl"

bind_spec(0) const f32 s_coverageScale     = 100;
bind_spec(1) const f32 s_coveragePanSpeedX = 1.5;
bind_spec(2) const f32 s_coveragePanSpeedY = 0.25;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_global(2) uniform sampler2D u_texGeoNormalTags;
bind_global(3) uniform sampler2D u_texGeoDepth;
bind_global(5) uniform sampler2DShadow u_texShadow;

bind_graphic(1) uniform sampler2D u_texCoverageMask;

bind_internal(0) in f32v2 in_texcoord;
bind_internal(1) in flat f32v3 in_direction;
bind_internal(2) in flat f32v3 in_radiance;
bind_internal(3) in flat f32m4 in_shadowViewProj;

bind_internal(0) out f32v4 out_color;

f32v3 clip_to_world(const f32v3 clipPos) {
  const f32v4 v = u_global.viewProjInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

f32 coverage_frac(const f32v3 worldPos) {
  const f32v2 texOffset = f32v2(s_coveragePanSpeedX, s_coveragePanSpeedY) * u_global.time.x;
  const f32v2 texCoord  = (worldPos.xz + texOffset) / s_coverageScale;
  return texture(u_texCoverageMask, texCoord).r;
}

f32 shadow_frac(const f32v3 worldPos) {
  const f32v4 shadowClipPos = in_shadowViewProj * f32v4(worldPos, 1.0);
  if (shadowClipPos.z <= 0.0) {
    return 0.0;
  }
  const f32v2 shadowTexelSize = 1.0 / textureSize(u_texShadow, 0);
  const f32v2 baseShadowCoord = shadowClipPos.xy * 0.5 + 0.5;

  f32 shadowSum = 0;
  for (i32 y = -1; y <= 1; y += 1) {
    for (i32 x = -1; x <= 1; x += 1) {
      const f32v2 shadowCoord = baseShadowCoord + f32v2(x, y) * shadowTexelSize;
      shadowSum += texture(u_texShadow, f32v3(shadowCoord, shadowClipPos.z));
    }
  }
  return shadowSum / 9;
}

void main() {
  const f32v4 colorRough = texture(u_texGeoColorRough, in_texcoord);
  const f32v4 normalTags = texture(u_texGeoNormalTags, in_texcoord);
  const f32   depth      = texture(u_texGeoDepth, in_texcoord).r;

  const f32v3 clipPos  = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world(clipPos);
  const f32v3 viewDir  = normalize(u_global.camPosition.xyz - worldPos);

  PbrSurface surf;
  surf.position     = worldPos;
  surf.color        = colorRough.rgb;
  surf.normal       = normal_tex_decode(normalTags.xyz);
  surf.roughness    = colorRough.a;
  surf.metallicness = 0.0; // TODO: Support metals.

  f32v3 effectiveRadiance = in_radiance * coverage_frac(worldPos);

  const bool hasShadowMap = in_shadowViewProj[3][3] > 0.0;
  if (hasShadowMap) {
    effectiveRadiance *= 1.0 - shadow_frac(worldPos);
  }

  out_color = f32v4(pbr_light_dir(effectiveRadiance, in_direction, viewDir, surf), 1.0);
}
