#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "quat.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_graphic_img(0) uniform sampler2D u_atlasColor;

bind_global_img(0) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_positionInv;    // -worldSpacePos.
bind_internal(1) in flat f32v4 in_rotationInv;    // inverse(worldSpaceRot).
bind_internal(2) in flat f32v3 in_scaleInv;       // 1.0 / worldSpaceScale.
bind_internal(3) in flat f32v4 in_atlasColorRect; // xy: origin, zw: scale.

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

  // Transform back to coordinates local to the unit cube.
  const f32v3 localPos = quat_rotate(in_rotationInv, worldPos + in_positionInv) * in_scaleInv;

  // Discard pixels outside of the decal space.
  const f32v3 absLocalPos = abs(localPos);
  if (absLocalPos.x > 0.5 || absLocalPos.y > 0.5 || absLocalPos.z > 0.5) {
    discard;
  }

  const f32v2 colorTexCoord = in_atlasColorRect.xy + (localPos.xz + 0.5) * in_atlasColorRect.zw;
  out_color                 = texture(u_atlasColor, colorTexCoord);
}
