#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

bind_spec(0) const bool s_onlyRChannel = false;

bind_graphic(1) uniform sampler2D u_tex;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v4 color = texture(u_tex, in_texcoord);
  out_color         = s_onlyRChannel ? f32v4(color.r) : color;
}
