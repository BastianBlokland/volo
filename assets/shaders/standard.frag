#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/texture.glsl"

bind_spec(0) const bool spec_shade = true;

bind_graphic(1) uniform sampler2D u_texDiffuse;

bind_internal(0) in f32_vec3 in_normal; // NOTE: non-normalized
bind_internal(1) in f32_vec2 in_texcoord;

bind_internal(0) out f32_vec4 out_color;

f32 compute_light_intensity(const f32_vec3 lightDir, const f32_vec3 normal) {
  if (spec_shade) {
    return clamp(dot(normal, lightDir), 0.1, 1.0);
  } else {
    return 1.0;
  }
}

void main() {
  const f32_vec3 lightDir       = normalize(f32_vec3(0.2, 1.0, -0.5));
  const f32      lightIntensity = compute_light_intensity(lightDir, in_normal);
  const f32_vec4 diffuse        = texture_sample_srgb(u_texDiffuse, in_texcoord);

  out_color = diffuse * lightIntensity;
}
