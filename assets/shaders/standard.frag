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
bind_internal(1) out f32v4 out_normalTags;

void main() {
  // Output color and roughness.
  out_colorRough = texture(u_texColorRough, in_texcoord);

  // Output world normal.
  if (s_normalMap) {
    const f32v3 normal = texture_normal(u_texNormal, in_texcoord, in_worldNormal, in_worldTangent);
    out_normalTags.xyz = normal_tex_encode(normal);
  } else {
    out_normalTags.xyz = normal_tex_encode(in_worldNormal);
  }

  // Output tags.
  out_normalTags.w = tags_tex_encode(in_tags);
}
