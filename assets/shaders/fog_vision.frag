#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

bind_internal(0) out f32 out_val;

void main() { out_val = 1.0; }
