#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

bind_graphic(0) uniform samplerCube u_texCubeMap;

bind_internal(0) in f32v3 in_viewDir; // NOTE: non-normalized

bind_internal(0) out f32v4 out_color;

void main() { out_color = texture(u_texCubeMap, -in_viewDir); }
