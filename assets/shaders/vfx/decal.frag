#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "geometry.glsl"
#include "global.glsl"
#include "quat.glsl"
#include "tags.glsl"

const f32 c_angleFadeMin = 0.2;
const f32 c_angleFadeMax = 0.8;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_graphic_img(0) uniform sampler2D u_atlasColor;

bind_global_img(0) uniform sampler2D u_texGeoData1;
bind_global_img(1) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_position;       // World-space.
bind_internal(1) in flat f32v4 in_rotation;       // World-space.
bind_internal(2) in flat f32v3 in_scale;          // World-space.
bind_internal(3) in flat f32v4 in_atlasColorRect; // xy: origin, zw: scale.

bind_internal(0) out f32v4 out_color;

f32v3 clip_to_world(const f32v3 clipPos) {
  const f32v4 v = u_global.viewProjInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

void main() {
  const f32v2 texcoord = in_fragCoord.xy / u_global.resolution.xy;
  const f32v4 geoData1 = texture(u_texGeoData1, texcoord);
  const f32   depth    = texture(u_texGeoDepth, texcoord).r;
  const u32   tags     = tags_tex_decode(geoData1.w);

  const f32v3 clipPos  = f32v3(texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world(clipPos);

  // Transform back to coordinates local to the unit cube.
  const f32v3 localPos = quat_rotate(quat_inverse(in_rotation), worldPos - in_position) / in_scale;

  // Discard pixels outside of the decal space or on top of a unit.
  const bool  isUnit      = tag_is_set(tags, tag_unit_bit);
  const f32v3 absLocalPos = abs(localPos);
  if (isUnit || absLocalPos.x > 0.5 || absLocalPos.y > 0.5 || absLocalPos.z > 0.5) {
    discard;
  }

  // Compute a fade-factor based on the difference in angle between the decal and the geometry.
  const f32v3 geoNormal   = geometry_decode_normal(geoData1);
  const f32v3 decalNormal = quat_rotate(in_rotation, f32v3(0, 1, 0));
  const f32 angleFade = smoothstep(c_angleFadeMax, c_angleFadeMin, 1 - dot(geoNormal, decalNormal));

  // Sample the color atlas.
  const f32v2 colorTexCoord    = in_atlasColorRect.xy + (localPos.xz + 0.5) * in_atlasColorRect.zw;
  const f32v4 colorAtlasSample = texture(u_atlasColor, colorTexCoord);

  out_color = f32v4(colorAtlasSample.rgb, colorAtlasSample.a * angleFade);
}
