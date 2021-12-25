#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/texture.glsl"

bind_graphic(1) uniform sampler2D texSampler;
bind_global_align(0) readonly uniform GlobalBuffer { f32_vec4 color; };

bind_internal(0) in f32_vec2 in_texcoord;
bind_internal(0) out f32_vec4 out_color;

void main() { out_color = texture_sample_srgb(texSampler, in_texcoord) * color; }
