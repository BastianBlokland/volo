#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

struct TonemapperData {
  f32 exposure;
  u32 mode;
};

const u32 c_modeLinear   = 0;
const u32 c_modeReinhard = 1;

bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_draw_data(0) readonly uniform Draw { TonemapperData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

/**
 * Linear.
 */
f32v3 tonemap_linear(const f32v3 colorHdr) { return clamp(colorHdr, 0, 1); }

/**
 * Reinhard.
 * Presented by Erik Reinhard at Siggraph 2002.
 */
f32v3 tonemap_reinhard(const f32v3 colorHdr) { return colorHdr / (f32v3(1.0) + colorHdr); }

void main() {
  const f32v3 colorHdr = texture(u_texGeoColorRough, in_texcoord).rgb * u_draw.exposure;

  f32v3 colorSdr;
  switch (u_draw.mode) {
  default:
  case c_modeLinear:
    colorSdr = tonemap_linear(colorHdr);
    break;
  case c_modeReinhard:
    colorSdr = tonemap_reinhard(colorHdr);
    break;
  }

  out_color = f32v4(colorSdr, 1.0);
}
