#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/color.glsl"
#include "include/types.glsl"

layout(location = 0) in f32_vec2 inTexcoord;

layout(location = 0) out f32_vec4 outColor;

void main() { outColor = color_fuchsia; }
