#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

bind_graphic(0) uniform sampler2D u_atlas;

bind_internal(0) in flat f32v4 in_color;
bind_internal(1) in flat f32 in_opacity;
bind_internal(2) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v4 texSample = texture(u_atlas, in_texcoord);

  const f32 alpha = texSample.a * in_color.a;
  out_color.rgb   = texSample.rgb * in_color.rgb * alpha;
  out_color.a     = alpha * in_opacity;
}
