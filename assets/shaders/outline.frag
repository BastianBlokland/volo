#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "tags.glsl"
#include "texture.glsl"

const f32   c_depthThreshold = 1e-4;
const f32v3 c_outlineColor   = f32v3(0.75, 0.75, 0.75);

bind_global_img(1) uniform sampler2D u_texGeoData1;
bind_global_img(2) uniform sampler2D u_texGeoDepth;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v3 out_color;

#define depth_at_offset(_OFFSET_X_, _OFFSET_Y_)                                                    \
  textureOffset(u_texGeoDepth, in_texcoord, i32v2(_OFFSET_X_, _OFFSET_Y_)).x

#define tags_at_offset(_OFFSET_X_, _OFFSET_Y_)                                                     \
  tags_tex_decode(textureOffset(u_texGeoData1, in_texcoord, i32v2(_OFFSET_X_, _OFFSET_Y_)).w)

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

  /**
   * Take 8 samples around this pixel, this introduces some minor artifacts around details that are
   * only a few pixels in size but for most objects this isn't very noticeable.
   *
   * 10101
   * 00000
   * 10x01
   * 00000
   * 10101
   */

  u32 neighborTags = 0;
  neighborTags |= (depth - depth_at_offset(0, 2)) < c_depthThreshold ? tags_at_offset(0, 2) : 0;
  neighborTags |= (depth - depth_at_offset(0, -2)) < c_depthThreshold ? tags_at_offset(0, -2) : 0;
  neighborTags |= (depth - depth_at_offset(2, 0)) < c_depthThreshold ? tags_at_offset(2, 0) : 0;
  neighborTags |= (depth - depth_at_offset(-2, 0)) < c_depthThreshold ? tags_at_offset(-2, 0) : 0;
  neighborTags |= (depth - depth_at_offset(2, 2)) < c_depthThreshold ? tags_at_offset(2, 2) : 0;
  neighborTags |= (depth - depth_at_offset(-2, 2)) < c_depthThreshold ? tags_at_offset(-2, 2) : 0;
  neighborTags |= (depth - depth_at_offset(2, -2)) < c_depthThreshold ? tags_at_offset(2, -2) : 0;
  neighborTags |= (depth - depth_at_offset(-2, -2)) < c_depthThreshold ? tags_at_offset(-2, -2) : 0;

  /**
   * Output white when any closer neighbor has the 'outline' tag set.
   */
  out_color = tag_is_set(neighborTags, tag_outline_bit) ? c_outlineColor : f32v3(0);
}
