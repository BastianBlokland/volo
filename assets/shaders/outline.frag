#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "tags.glsl"
#include "texture.glsl"

bind_global(2) uniform sampler2D u_texGeoNormalTags;
bind_global(3) uniform sampler2D u_texGeoDepth;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v3 out_color;

#define depth_at_offset(_OFFSET_X_, _OFFSET_Y_)                                                    \
  textureOffset(u_texGeoDepth, in_texcoord, i32v2(_OFFSET_X_, _OFFSET_Y_)).x

#define tags_at_offset(_OFFSET_X_, _OFFSET_Y_)                                                     \
  tags_tex_decode(textureOffset(u_texGeoNormalTags, in_texcoord, i32v2(_OFFSET_X_, _OFFSET_Y_)).w)

void main() {
  /**
   * Draw a white border around pixels that have the 'outline' tag set.
   * Exclude neighbors that are further away then the current pixel;
   */

  const f32 depth = depth_at_offset(0, 0);
  const u32 tags  = tags_at_offset(0, 0);

  if (tag_is_set(tags, tag_outline_bit)) {
    out_color = f32v3(0);
    return;
  }

  const f32 depthThreshold = 1e-4;

  u32 neighborTags = 0;
  neighborTags |= (depth - depth_at_offset(2, 2)) < depthThreshold ? tags_at_offset(2, 2) : 0;
  neighborTags |= (depth - depth_at_offset(-2, 2)) < depthThreshold ? tags_at_offset(-2, 2) : 0;
  neighborTags |= (depth - depth_at_offset(2, -2)) < depthThreshold ? tags_at_offset(2, -2) : 0;
  neighborTags |= (depth - depth_at_offset(-2, -2)) < depthThreshold ? tags_at_offset(-2, -2) : 0;

  /**
   * Output white when any closer neighbor has the 'outline' tag set.
   */
  out_color = f32v3(tag_is_set(neighborTags, tag_outline_bit));
}
