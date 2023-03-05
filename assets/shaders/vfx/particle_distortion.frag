#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

bind_graphic_img(0) uniform sampler2D u_atlas;

bind_internal(0) in flat f32v4 in_color;
bind_internal(3) in f32v2 in_texcoord;

bind_internal(0) out f32v2 out_distortion;

void main() {
  const f32v4 texSample  = texture(u_atlas, in_texcoord);
  const f32v3 distNormal = normal_tex_decode(texSample.rgb);
  const f32   strength   = texSample.a * in_color.a;

  out_distortion = distNormal.xy * strength;
}
