#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v3 out_color;

void main() { out_color = f32v3(in_texcoord, 1); }
