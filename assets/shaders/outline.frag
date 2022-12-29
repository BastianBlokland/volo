#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "tags.glsl"
#include "texture.glsl"

bind_global(2) uniform sampler2D u_texGeoNormalTags;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

#define tags_at_offset(_OFFSET_X_, _OFFSET_Y_)                                                     \
  tags_tex_decode(textureOffset(u_texGeoNormalTags, in_texcoord, i32v2(_OFFSET_X_, _OFFSET_Y_)).w)

void main() {
  /**
   * Output white when this fragment is not part of the outline object but a neighbor is.
   */
  u32 tags = 0;
  tags |= tags_at_offset(2, 2);
  tags |= tags_at_offset(-2, 2);
  tags |= tags_at_offset(2, -2);
  tags |= tags_at_offset(-2, -2);

  tags ^= tags_at_offset(0, 0);

  out_color.rgb = f32v3(tag_is_set(tags, tag_selected_bit));
}
