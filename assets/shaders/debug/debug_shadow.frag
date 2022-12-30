#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

bind_global(5) uniform sampler2D u_texShadow;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() { out_color = f32v4(texture(u_texShadow, in_texcoord).r); }
