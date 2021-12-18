#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/types.glsl"

layout(location = 0) in f32_vec3 fragColor;

layout(location = 0) out f32_vec4 outColor;

void main() { outColor = f32_vec4(fragColor, 1.0); }
