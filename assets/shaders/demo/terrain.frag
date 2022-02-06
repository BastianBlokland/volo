#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

bind_graphic(2) uniform sampler2D u_texDiffuse;

bind_internal(2) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() { out_color = texture_sample_srgb(u_texDiffuse, in_texcoord); }
