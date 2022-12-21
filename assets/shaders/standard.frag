#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "tags.glsl"
#include "texture.glsl"

bind_spec(0) const bool s_normalMap = false;

bind_graphic(1) uniform sampler2D u_texColorRough;
bind_graphic(2) uniform sampler2D u_texNormal;

bind_internal(0) in f32v3 in_worldNormal;  // NOTE: non-normalized
bind_internal(1) in f32v4 in_worldTangent; // NOTE: non-normalized
bind_internal(2) in f32v2 in_texcoord;
bind_internal(3) in flat u32 in_tags;

bind_internal(0) out f32v4 out_colorRough;
bind_internal(1) out f32v3 out_normal;

void main() {
  out_colorRough = texture(u_texColorRough, in_texcoord);
  if (tag_is_set(in_tags, tag_selected_bit)) {
    out_colorRough.rgb += 1.0f;
  }
  if (tag_is_set(in_tags, tag_damaged_bit)) {
    out_colorRough.rgb += f32v3(0.4, 0.025, 0.025);
  }

  if (s_normalMap) {
    out_normal = texture_normal(u_texNormal, in_texcoord, in_worldNormal, in_worldTangent);
  } else {
    out_normal = normalize(in_worldNormal);
  }
}
