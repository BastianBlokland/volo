#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "tags.glsl"
#include "texture.glsl"

struct AmbientData {
  f32v4 packed; // x: ambientLight, y: mode, z, flags, w: unused
};

bind_spec(0) const bool s_debug = false;

const u32 c_modeSolid                 = 0;
const u32 c_modeDiffuseIrradiance     = 1;
const u32 c_modeDebugColor            = 2;
const u32 c_modeDebugRoughness        = 3;
const u32 c_modeDebugNormal           = 4;
const u32 c_modeDebugDepth            = 5;
const u32 c_modeDebugTags             = 6;
const u32 c_modeDebugAmbientOcclusion = 7;

const u32 c_flagsAmbientOcclusion     = 1 << 0;
const u32 c_flagsAmbientOcclusionBlur = 1 << 1;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_global(2) uniform sampler2D u_texGeoNormalTags;
bind_global(3) uniform sampler2D u_texGeoDepth;
bind_global(4) uniform sampler2D u_texAmbientOcclusion;
bind_graphic(0) uniform samplerCube u_texDiffuseIrradiance;
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

void main() {
  const f32v4 colorRough = texture(u_texGeoColorRough, in_texcoord);
  const f32v4 normalTags = texture(u_texGeoNormalTags, in_texcoord);
  const f32   depth      = texture(u_texGeoDepth, in_texcoord).r;

  const f32v3 color        = colorRough.rgb;
  const f32   roughness    = colorRough.a;
  const u32   tags         = tags_tex_decode(normalTags.w);
  const f32v3 clipPos      = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos     = clip_to_world(clipPos);
  const f32v3 normal       = normal_tex_decode(normalTags.xyz);
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
    const f32 linearDepth = clip_to_view(clipPos).z;
    switch (mode) {
    case c_modeDebugColor:
      out_color = color;
      break;
    case c_modeDebugRoughness:
      out_color = roughness.rrr;
      break;
    case c_modeDebugNormal:
      out_color = normal;
      break;
    case c_modeDebugDepth:
      const f32 debugMaxDist = 100.0;
      out_color              = linearDepth.rrr / debugMaxDist;
      break;
    case c_modeDebugTags:
      out_color = color_from_hsv(tags / 255.0, 1, 1);
      break;
    case c_modeDebugAmbientOcclusion:
      out_color = ambientOcclusion.rrr;
      break;
    default:
      discard;
    }
  } else {
    switch (mode) {
    case c_modeSolid:
      out_color = color * ambientLight * ambientOcclusion;
      break;
    case c_modeDiffuseIrradiance:
      const f32v3 diffuseIrradiance = texture_cube(u_texDiffuseIrradiance, normal).rgb;
      out_color                     = color * diffuseIrradiance * ambientLight * ambientOcclusion;
      break;
    default:
      discard;
    }

    // Additional effects.
    if (tag_is_set(tags, tag_damaged_bit)) {
      out_color = mix(out_color, f32v3(0.8, 0.1, 0.1), abs(dot(normal, viewDir)));
    }
  }
}
