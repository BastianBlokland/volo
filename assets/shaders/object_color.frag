#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/types.glsl"

layout(location = 0) in f32_vec2 inTexcoord;

layout(location = 0) out f32_vec4 outColor;

void main() { outColor = f32_vec4(1, 1, 1, 1); }
