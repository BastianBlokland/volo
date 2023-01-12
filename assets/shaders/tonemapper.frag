#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

bind_global(1) uniform sampler2D u_texGeoColorRough;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v3 colorHdr = texture(u_texGeoColorRough, in_texcoord).rgb;
  const f32v3 colorSdr = clamp(colorHdr, 0, 1);

  out_color = f32v4(colorSdr, 1.0);
}
