#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

bind_internal(0) out f32v4 out_color;

void main() { out_color = f32v4(1, 1, 1, 1); }
