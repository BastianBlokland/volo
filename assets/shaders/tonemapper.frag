#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

struct TonemapperData {
  f32 exposure;
};

bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_draw_data(0) readonly uniform Draw { TonemapperData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 map_linear(const f32v3 colorHdr) { return clamp(colorHdr * u_draw.exposure, 0, 1); }

void main() {
  const f32v3 colorHdr = texture(u_texGeoColorRough, in_texcoord).rgb * u_draw.exposure;
  const f32v3 colorSdr = map_linear(colorHdr);

  out_color = f32v4(colorSdr, 1.0);
}
