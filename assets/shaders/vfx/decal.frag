#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "geometry.glsl"
#include "global.glsl"
#include "math_frag.glsl"
#include "quat.glsl"
#include "tag.glsl"

const f32 c_fadeAngleMin = 0.2;
const f32 c_fadeAngleMax = 0.8;

const u32 c_flagOutputColor           = 1 << 0;
const u32 c_flagOutputNormal          = 1 << 1;
const u32 c_flagGBufferBaseNormal     = 1 << 2;
const u32 c_flagDepthBufferBaseNormal = 1 << 3;
const u32 c_flagFadeUsingDepthNormal  = 1 << 4;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_graphic_img(0) uniform sampler2D u_atlasColor;
bind_graphic_img(1) uniform sampler2D u_atlasNormal;

bind_global_img(0) uniform sampler2D u_texGeoData1;
bind_global_img(1) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_position;        // World-space.
bind_internal(1) in flat f32v4 in_rotation;        // World-space.
bind_internal(2) in flat f32v3 in_scale;           // World-space.
bind_internal(3) in flat f32v3 in_atlasColorMeta;  // xy: origin, z: scale.
bind_internal(4) in flat f32v3 in_atlasNormalMeta; // xy: origin, z: scale.
bind_internal(5) in flat u32 in_flags;
bind_internal(6) in flat f32 in_roughness;
bind_internal(7) in flat f32 in_alpha;
bind_internal(8) in flat u32 in_excludeTags;

/**
 * Geometry Data0: color (rgb), emissive (a).
 * Geometry Data1: normal (rg), roughness (b) and tags (a).
 * Alpha blended, w is used to control the blending, outputting emissive / tags is not supported.
 *
 * NOTE: Normals can only be blended (without discontinuities) if the source and destination both
 * have a positive y value or both a negative value. Reason for this is that we use a octahedron
 * normal encoding.
 */
bind_internal(0) out f32v4 out_data0;
bind_internal(1) out f32v4 out_data1;

f32v4 atlas_sample(const sampler2D atlas, const f32v3 atlasMeta, const f32v3 decalPos) {
  // NOTE: Flip the Y component as we are using the bottom as the texture origin.
  const f32v2 texcoord = atlasMeta.xy + (f32v2(decalPos.x, -decalPos.y) + 0.5) * atlasMeta.z;
  return texture(atlas, texcoord);
}

f32v3 atlas_sample_normal(const sampler2D atlas, const f32v3 atlasMeta, const f32v3 decalPos) {
  return normal_tex_decode(atlas_sample(atlas, atlasMeta, decalPos).xyz);
}

f32v3 flat_normal_from_position(const f32v3 pos) {
  const f32v3 deltaPosX = dFdx(pos);
  const f32v3 deltaPosY = dFdy(pos);
  return normalize(cross(deltaPosX, deltaPosY));
}

void main() {
  const f32v2 texcoord = in_fragCoord.xy / u_global.resolution.xy;
  const f32v4 geoData1 = texture(u_texGeoData1, texcoord);
  const f32   depth    = texture(u_texGeoDepth, texcoord).r;
  const u32   tags     = tags_tex_decode(geoData1.w);

  const f32v3 clipPos  = f32v3(texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world_pos(u_global, clipPos);

  // Transform back to coordinates local to the unit cube.
  const f32v3 localPos = quat_rotate(quat_inverse(in_rotation), worldPos - in_position) / in_scale;

  // Discard pixels on invalid surface or outside of the decal space.
  const bool  validSurface = (tags & in_excludeTags) == 0;
  const f32v3 absLocalPos  = abs(localPos);
  if (!validSurface || absLocalPos.x > 0.5 || absLocalPos.y > 0.5 || absLocalPos.z > 0.5) {
    discard;
  }

  const f32v3 geoNormal   = geometry_decode_normal(geoData1);
  const f32v3 decalNormal = quat_rotate(in_rotation, f32v3(0, 0, 1));
  const f32v3 depthNormal = flat_normal_from_position(worldPos);
  f32v3       baseNormal;
  if ((in_flags & c_flagGBufferBaseNormal) != 0) {
    baseNormal = geoNormal;
  } else if ((in_flags & c_flagDepthBufferBaseNormal) != 0) {
    baseNormal = depthNormal;
  } else {
    baseNormal = decalNormal;
  }

  // Compute a fade-factor based on the difference in angle between the decal and the geometry.
  const f32v3 fadeNormal = (in_flags & c_flagFadeUsingDepthNormal) != 0 ? depthNormal : geoNormal;
  const f32   fade = smoothstep(c_fadeAngleMax, c_fadeAngleMin, 1 - dot(fadeNormal, decalNormal));

  // Sample the color atlas.
  const f32v4 color = atlas_sample(u_atlasColor, in_atlasColorMeta, localPos);

  // Sample the normal atlas.
  f32v3 normal;
  if ((in_flags & c_flagOutputNormal) != 0) {
    const f32v3 tangentNormal = atlas_sample_normal(u_atlasNormal, in_atlasNormalMeta, localPos);
    normal                    = math_perturb_normal(tangentNormal, baseNormal, worldPos, texcoord);
  } else {
    normal = baseNormal;
  }

  // Output the result into the gbuffer.
  if ((in_flags & c_flagOutputColor) != 0) {
    out_data0 = f32v4(color.rgb, color.a * fade * in_alpha);
  } else {
    out_data0 = f32v4(0);
  }
  out_data1 = f32v4(math_normal_encode(normal), in_roughness, color.a * fade * in_alpha);
}
