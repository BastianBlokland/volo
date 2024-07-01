#include "binding.glsl"
#include "texture.glsl"

struct TonemapperData {
  f32 exposure;
  u32 mode;
  f32 bloomIntensity;
};

const u32 c_modeLinear        = 0;
const u32 c_modeLinearSmooth  = 1;
const u32 c_modeReinhard      = 2;
const u32 c_modeReinhardJodie = 3;
const u32 c_modeAces          = 4;

bind_global_img(0) uniform sampler2D u_texHdr;
bind_global_img(1) uniform sampler2D u_texBloom;
bind_global_img(2) uniform sampler2D u_texDistortion;
bind_draw_data(0) readonly uniform Draw { TonemapperData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32 luminance(const f32v3 v) { return dot(v, f32v3(0.2126, 0.7152, 0.0722)); }

/**
 * Linear with shoulder region.
 * By user SteveM in comment section on https://mynameismjp.wordpress.com.
 * https://mynameismjp.wordpress.com/2010/04/30/a-closer-look-at-tone-mapping/#comment-118287
 */
f32v3 tonemap_linear_smooth(const f32v3 hdr) {
  const f32 a = 1.8; // mid.
  const f32 b = 1.4; // toe.
  const f32 c = 0.5; // shoulder.
  const f32 d = 1.5; // mid.
  return (hdr * (a * hdr + b)) / (hdr * (a * hdr + c) + d);
}

/**
 * Reinhard.
 * Presented by Erik Reinhard at Siggraph 2002.
 */
f32v3 tonemap_reinhard(const f32v3 hdr) { return hdr / (f32v3(1.0) + hdr); }

/**
 * Reinhard Jodie.
 * Mix of tone-mapping individual channels and tone mapping luminance by user Jodie on ShaderToy:
 * https://www.shadertoy.com/view/4dBcD1
 */
f32v3 tonemap_reinhard_jodie(const f32v3 hdr) {
  const f32   lum = luminance(hdr);
  const f32v3 tv  = hdr / (1.0 + hdr);
  return mix(hdr / (1.0 + lum), tv, tv);
}

/**
 * ACES - (Academy Color Encoding System) Filmic Tone Mapping Curve.
 * Approximated ACES fit by Krzysztof Narkowicz.
 * https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
 */
f32v3 tonemap_aces_approx(const f32v3 hdr) {
  const f32 a = 2.51;
  const f32 b = 0.03;
  const f32 c = 2.43;
  const f32 d = 0.59;
  const f32 e = 0.14;
  return clamp((hdr * (a * hdr + b)) / (hdr * (c * hdr + d) + e), 0.0, 1.0);
}

/**
 * Apply screen-space distortion.
 */
f32v2 distortion_apply(const f32v2 coord) { return coord + texture(u_texDistortion, coord).xy; }

void main() {
  const f32v2 distortedCoord = distortion_apply(in_texcoord);
  const f32v3 colorHdrInput  = texture(u_texHdr, distortedCoord).rgb;
  const f32v3 bloomInput     = texture(u_texBloom, distortedCoord).rgb;
  const f32v3 colorHdr = mix(colorHdrInput, bloomInput, u_draw.bloomIntensity) * u_draw.exposure;

  f32v3 colorSdr;
  switch (u_draw.mode) {
  default:
  case c_modeLinear:
    colorSdr = colorHdr;
    break;
  case c_modeLinearSmooth:
    colorSdr = tonemap_linear_smooth(colorHdr);
    break;
  case c_modeReinhard:
    colorSdr = tonemap_reinhard(colorHdr);
    break;
  case c_modeReinhardJodie:
    colorSdr = tonemap_reinhard_jodie(colorHdr);
    break;
  case c_modeAces:
    colorSdr = tonemap_aces_approx(colorHdr);
    break;
  }

  out_color = f32v4(colorSdr, 1.0);
}
