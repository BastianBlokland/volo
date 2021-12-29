#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/texture.glsl"

bind_graphic(1) uniform sampler2D texDiffuse;

bind_internal(0) in f32_vec3 in_worldNormal;
bind_internal(1) in f32_vec2 in_texcoord;

bind_internal(0) out f32_vec4 out_color;

void main() {
  const f32_vec3 lightWorldDir  = normalize(f32_vec3(0.2, 1.0, -0.5));
  const f32      lightIntensity = clamp(dot(in_worldNormal, lightWorldDir), 0.1, 1.0);
  const f32_vec4 diffuse        = texture_sample_srgb(texDiffuse, in_texcoord);

  out_color = diffuse * lightIntensity;
}
