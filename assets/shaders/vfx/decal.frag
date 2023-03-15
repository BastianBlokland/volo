#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

bind_graphic_img(0) uniform sampler2D u_atlasColor;

bind_internal(0) in f32v2 in_texcoordColor;

bind_internal(0) out f32v4 out_color;

void main() { out_color = texture(u_atlasColor, in_texcoordColor); }
