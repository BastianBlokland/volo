#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/texture.glsl"
#include "include/types.glsl"

layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(location = 0) in f32_vec2 inTexcoord;

layout(location = 0) out f32_vec4 outColor;

void main() { outColor = texture_sample_srgb(texSampler, inTexcoord); }
