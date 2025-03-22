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

bind_global_img(0) uniform sampler2D u_texGeoBase;
bind_global_img(1) uniform sampler2D u_texGeoNormal;
bind_global_img(2) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_position;        // World-space.
bind_internal(1) in flat f32v4 in_rotation;        // World-space.
bind_internal(2) in flat f32v3 in_scale;           // World-space.
bind_internal(3) in flat f32v3 in_atlasColorMeta;  // xy: origin, z: scale.
bind_internal(4) in flat f32v3 in_atlasNormalMeta; // xy: origin, z: scale.
bind_internal(5) in flat u32 in_flags;
bind_internal(6) in flat f32 in_roughness;
bind_internal(7) in flat f32v2 in_alpha; // x: alphaBegin, y: alphaEnd.
bind_internal(8) in flat u32 in_excludeTags;
bind_internal(9) in flat f32v4 in_texTransform; // xy: offset, zw: scale.
bind_internal(10) in flat f32v4 in_warpP01;     // bottom left and bottom right.
bind_internal(11) in flat f32v4 in_warpP23;     // top left and top right.

/**
 * Geometry Base:      [r] color     [g] color     [b] color    [a] tags
 * Geometry Normal:    [r] normal    [g] normal
 * Geometry Attribute: [r] roughness [g] unused
 * Alpha blended, w is used to control the blending hence outputting tags is not supported.
 *
 * NOTE: Normals can only be blended (without discontinuities) if the source and destination both
 * have a positive y value or both a negative value. Reason for this is that we use a octahedron
 * normal encoding.
 */
bind_internal(0) out f32v4 out_base;
bind_internal(1) out f32v4 out_normal;
bind_internal(2) out f32v4 out_attribute;

f32v4 atlas_sample(const sampler2D atlas, const f32v3 atlasMeta, const f32v2 atlasCoord) {
  // NOTE: Flip the Y component as we are using the bottom as the texture origin.
  const f32v2 texcoord = atlasMeta.xy + f32v2(atlasCoord.x, 1.0 - atlasCoord.y) * atlasMeta.z;
  return texture(atlas, texcoord);
}

f32v3 atlas_sample_normal(const sampler2D atlas, const f32v3 atlasMeta, const f32v2 atlasCoord) {
  return normal_tex_decode(atlas_sample(atlas, atlasMeta, atlasCoord).xyz);
}

f32v3 flat_normal_from_position(const f32v3 pos) {
  const f32v3 deltaPosX = dFdx(pos);
  const f32v3 deltaPosY = dFdy(pos);
  return normalize(cross(deltaPosX, deltaPosY));
}

/**
 * 2D wedge product (Grassmann algebra).
 * Reference: http://www.terathon.com/gdc12_lengyel.pdf
 */
f32 wedge2(const f32v2 v, const f32v2 w) { return v.x * w.y - v.y * w.x; }

f32v3 project_warp(const f32v3 stampPos) {
  /**
   * Inverse bilinear interpolation to warp the corners of the stamp quad.
   * Reference: https://www.reedbeta.com/blog/quadrilateral-interpolation-part-2/
   */
  const f32v2 q  = stampPos.xy - in_warpP01.xy;
  const f32v2 b1 = in_warpP01.zw - in_warpP01.xy;
  const f32v2 b2 = in_warpP23.zw - in_warpP01.xy;
  const f32v2 b3 = in_warpP01.xy - in_warpP01.zw - in_warpP23.zw + in_warpP23.xy;

  const f32 a = wedge2(b2, b3);
  const f32 b = wedge2(b3, q) - wedge2(b1, b2);
  const f32 c = wedge2(b1, q);

  f32 texcoordY;
  if (abs(a) < 0.001) /* Linear form */ {
    texcoordY = -c / b;
  } else /* Quadratic form */ {
    const f32 discrim = b * b - 4 * a * c;
    texcoordY         = 0.5 * (-b + sqrt(discrim)) / a;
  }

  // Solve for x, using largest-magnitude component.
  f32         texcoordX;
  const f32v2 denom = b1 + texcoordY * b3;
  if (abs(denom.x) > abs(denom.y)) {
    texcoordX = (q.x - b2.x * texcoordY) / denom.x;
  } else {
    texcoordX = (q.y - b2.y * texcoordY) / denom.y;
  }
  return f32v3(texcoordX, texcoordY, stampPos.z);
}

f32v3 project_box(const f32v3 worldPos) {
  const f32v3 boxPos = quat_rotate(quat_inverse(in_rotation), worldPos - in_position) / in_scale;
  return project_warp(boxPos + 0.5 /* Move the center from (0, 0, 0) to (0.5, 0.5, 0.5) */);
}

f32v2 stamp_texcoord(const f32v3 stampPos) {
  const f32v2 texOffset = in_texTransform.xy;
  const f32v2 texScale  = in_texTransform.zw;
  return mod(texOffset + stampPos.xy * texScale, 1.0);
}

f32v3 base_normal(const f32v3 geoNormal, const f32v3 stampNormal, const f32v3 depthNormal) {
  if ((in_flags & c_flagGBufferBaseNormal) != 0) {
    return geoNormal;
  }
  if ((in_flags & c_flagDepthBufferBaseNormal) != 0) {
    return depthNormal;
  }
  return stampNormal;
}

f32v3 fade_normal(const f32v3 geoNormal, const f32v3 depthNormal) {
  if ((in_flags & c_flagFadeUsingDepthNormal) != 0) {
    return depthNormal;
  }
  return geoNormal;
}

void main() {
  const f32v2 texcoord  = in_fragCoord.xy / u_global.resolution.xy;
  const f32v4 geoBase   = texture(u_texGeoBase, texcoord);
  const f32v3 geoNormal = geometry_decode_normal(texture(u_texGeoNormal, texcoord));
  const f32   depth     = texture(u_texGeoDepth, texcoord).r;
  const u32   tags      = tags_tex_decode(geoBase.w);

  const f32v3 clipPos  = f32v3(texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world_pos(u_global, clipPos);

  // Project the coordinates to the stamp box.
  const f32v3 stampPos = project_box(worldPos);

  // Discard pixels on invalid surface or outside of the stamp space.
  const bool valid = (tags & in_excludeTags) == 0;
  if (!valid || any(lessThan(stampPos, f32v3(0))) || any(greaterThan(stampPos, f32v3(1, 1, 1)))) {
    discard;
  }
  const f32v2 stampCoord = stamp_texcoord(stampPos);

  const f32v3 stampNormal = quat_rotate(in_rotation, f32v3(0, 0, 1));
  const f32v3 depthNormal = flat_normal_from_position(worldPos);

  const f32v3 baseNormal = base_normal(geoNormal, stampNormal, depthNormal);
  const f32v3 fadeNormal = fade_normal(geoNormal, depthNormal);

  // Compute a fade-factor based on the difference in angle between the stamp and the geometry.
  const f32 fade = smoothstep(c_fadeAngleMax, c_fadeAngleMin, 1 - dot(fadeNormal, stampNormal));

  // Sample the color atlas.
  const f32v4 color = atlas_sample(u_atlasColor, in_atlasColorMeta, stampCoord);

  // Sample the normal atlas.
  f32v3 normal;
  if ((in_flags & c_flagOutputNormal) != 0) {
    const f32v3 tangentNormal = atlas_sample_normal(u_atlasNormal, in_atlasNormalMeta, stampCoord);
    normal                    = math_perturb_normal(tangentNormal, baseNormal, worldPos, texcoord);
  } else {
    normal = baseNormal;
  }

  const f32 alpha = color.a * fade * mix(in_alpha.x, in_alpha.y, stampPos.y);

  // Output the result into the gbuffer.
  if ((in_flags & c_flagOutputColor) != 0) {
    out_base = f32v4(color.rgb, alpha);
  } else {
    out_base = f32v4(0);
  }
  out_normal    = f32v4(math_normal_encode(normal), 0, alpha);
  out_attribute = f32v4(in_roughness, 0, 0, alpha);
}
