#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

bind_global(1) uniform sampler2D u_texGeoColor;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() { out_color = texture(u_texGeoColor, in_texcoord); }
