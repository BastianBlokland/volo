#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "tags.glsl"
#include "texture.glsl"

struct AmbientData {
  f32v4 packed; // x: ambientLight, y: mode, z, flags, w: unused
};

bind_spec(0) const bool s_debug             = false;
bind_spec(1) const f32 s_specIrradianceMips = 5.0;

const u32 c_modeSolid                   = 0;
const u32 c_modeDiffuseIrradiance       = 1;
const u32 c_modeSpecularIrradiance      = 2;
const u32 c_modeDebugColor              = 3;
const u32 c_modeDebugRoughness          = 4;
const u32 c_modeDebugNormal             = 5;
const u32 c_modeDebugDepth              = 6;
const u32 c_modeDebugTags               = 7;
const u32 c_modeDebugAmbientOcclusion   = 8;
const u32 c_modeDebugFresnel            = 9;
const u32 c_modeDebugDiffuseIrradiance  = 10;
const u32 c_modeDebugSpecularIrradiance = 11;

const u32 c_flagsAmbientOcclusion     = 1 << 0;
const u32 c_flagsAmbientOcclusionBlur = 1 << 1;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_global_img(0) uniform sampler2D u_texGeoColorRough;
bind_global_img(1) uniform sampler2D u_texGeoNormalTags;
bind_global_img(2) uniform sampler2D u_texGeoDepth;
bind_global_img(3) uniform sampler2D u_texAmbientOcclusion;

bind_graphic_img(0) uniform samplerCube u_texDiffIrradiance;
bind_graphic_img(1) uniform samplerCube u_texSpecIrradiance;
bind_graphic_img(2) uniform sampler2D u_texBrdfIntegration;

bind_draw_data(0) readonly uniform Draw { AmbientData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v3 out_color;

f32 ao_sample_single() { return texture(u_texAmbientOcclusion, in_texcoord).r; }

f32 ao_sample_blur() {
  const f32v2 aoTexelSize = 1.0 / f32v2(textureSize(u_texAmbientOcclusion, 0));
  f32         aoSum       = 0.0;
  for (i32 x = -1; x <= 1; ++x) {
    for (i32 y = -1; y <= 1; ++y) {
      const f32v2 aoCoord = in_texcoord + f32v2(x, y) * aoTexelSize;
      aoSum += texture(u_texAmbientOcclusion, aoCoord).r;
    }
  }
  return aoSum / 9;
}

f32v3 clip_to_view(const f32v3 clipPos) {
  const f32v4 v = u_global.projInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

f32v3 clip_to_world(const f32v3 clipPos) {
  const f32v4 v = u_global.viewProjInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

f32v3 ambient_diff_irradiance(const PbrSurface surf, const f32 intensity) {
  return texture_cube(u_texDiffIrradiance, surf.normal).rgb * intensity;
}

f32v3 ambient_spec_irradiance(
    const PbrSurface surf,
    const f32        intensity,
    const f32        nDotV,
    const f32v3      fresnel,
    const f32v3      viewDir) {
  const f32v3 reflectDir         = reflect(-viewDir, surf.normal);
  const f32   mip                = surf.roughness * s_specIrradianceMips;
  const f32v3 filteredIrradiance = texture_cube_lod(u_texSpecIrradiance, reflectDir, mip).rgb;
  const f32v2 brdf               = texture(u_texBrdfIntegration, f32v2(nDotV, surf.roughness)).rg;
  return filteredIrradiance * (fresnel * brdf.x + brdf.y) * intensity;
}

void main() {
  const f32v4 colorRough = texture(u_texGeoColorRough, in_texcoord);
  const f32v4 normalTags = texture(u_texGeoNormalTags, in_texcoord);
  const f32   depth      = texture(u_texGeoDepth, in_texcoord).r;

  const u32   tags     = tags_tex_decode(normalTags.w);
  const f32v3 clipPos  = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world(clipPos);

  PbrSurface surf;
  surf.position     = worldPos;
  surf.color        = colorRough.rgb;
  surf.normal       = normal_tex_decode(normalTags.xyz);
  surf.roughness    = colorRough.a;
  surf.metallicness = 0.0; // TODO: Support metals.

  const f32v3 viewDir      = normalize(u_global.camPosition.xyz - worldPos);
  const f32   ambientLight = u_draw.packed.x;
  const u32   mode         = floatBitsToUint(u_draw.packed.y);
  const u32   flags        = floatBitsToUint(u_draw.packed.z);

  f32 ambientOcclusion;
  if ((flags & c_flagsAmbientOcclusion) == 0) {
    ambientOcclusion = 1.0;
  } else if ((flags & c_flagsAmbientOcclusionBlur) != 0) {
    ambientOcclusion = ao_sample_blur();
  } else {
    ambientOcclusion = ao_sample_single();
  }

  if (s_debug) {
    switch (mode) {
    case c_modeDebugColor:
      out_color = surf.color;
      break;
    case c_modeDebugRoughness:
      out_color = surf.roughness.rrr;
      break;
    case c_modeDebugNormal:
      out_color = surf.normal;
      break;
    case c_modeDebugDepth: {
      const f32 debugMaxDist = 100.0;
      const f32 linearDepth  = clip_to_view(clipPos).z;
      out_color              = linearDepth.rrr / debugMaxDist;
    } break;
    case c_modeDebugTags:
      out_color = color_from_hsv(tags / 255.0, 1, 1);
      break;
    case c_modeDebugAmbientOcclusion:
      out_color = ambientOcclusion.rrr;
      break;
    case c_modeDebugFresnel: {
      const f32v3 reflectance = pbr_surf_reflectance(surf);
      const f32   nDotV       = max(dot(surf.normal, viewDir), 0);
      out_color               = pbr_fresnel_schlick_atten(nDotV, reflectance, surf.roughness);
    } break;
    case c_modeDebugDiffuseIrradiance:
      out_color = ambient_diff_irradiance(surf, ambientLight);
      break;
    case c_modeDebugSpecularIrradiance: {
      const f32v3 reflectance = pbr_surf_reflectance(surf);
      const f32   nDotV       = max(dot(surf.normal, viewDir), 0);
      const f32v3 fresnel     = pbr_fresnel_schlick_atten(nDotV, reflectance, surf.roughness);
      out_color = ambient_spec_irradiance(surf, ambientLight, nDotV, fresnel, viewDir);
    } break;
    }
  } else {

    // Ambient light.
    switch (mode) {
    case c_modeSolid:
      out_color = surf.color * ambientLight * ambientOcclusion;
      break;
    case c_modeDiffuseIrradiance:
    case c_modeSpecularIrradiance: {
      const f32v3 reflectance    = pbr_surf_reflectance(surf);
      const f32   nDotV          = max(dot(surf.normal, viewDir), 0);
      const f32v3 fresnel        = pbr_fresnel_schlick_atten(nDotV, reflectance, surf.roughness);
      const f32v3 diffIrradiance = ambient_diff_irradiance(surf, ambientLight);
      out_color                  = (1.0 - fresnel) * diffIrradiance * surf.color * ambientOcclusion;

      if (mode == c_modeSpecularIrradiance) {
        const f32v3 spec = ambient_spec_irradiance(surf, ambientLight, nDotV, fresnel, viewDir);
        out_color += spec * ambientOcclusion;
      }
    } break;
    }

    // Additional effects.
    if (tag_is_set(tags, tag_damaged_bit)) {
      out_color = mix(out_color, f32v3(0.8, 0.1, 0.1), abs(dot(surf.normal, viewDir)));
    }
  }
}
