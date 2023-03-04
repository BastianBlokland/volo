#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "geometry.glsl"
#include "rand.glsl"
#include "tags.glsl"

const f32 c_alphaTextureThreshold = 0.2;
const f32 c_alphaDitherMax        = 0.99;

bind_spec(0) const bool s_normalMap = false;
bind_spec(1) const bool s_alphaMap  = false;
bind_spec(2) const bool s_emissive  = false;

bind_graphic_img(0) uniform sampler2D u_texColorRough;
bind_graphic_img(1) uniform sampler2D u_texNormalEmissive;
bind_graphic_img(2) uniform sampler2D u_texAlpha;

bind_internal(0) in f32v3 in_worldNormal;  // NOTE: non-normalized
bind_internal(1) in f32v4 in_worldTangent; // NOTE: non-normalized
bind_internal(2) in f32v2 in_texcoord;
bind_internal(3) in flat f32v4 in_data;

bind_internal(0) out f32v4 out_data0;
bind_internal(1) out f32v4 out_data1;

void main() {
  f32 alpha = in_data.y;
  if (s_alphaMap) {
    if (texture(u_texAlpha, in_texcoord).r < c_alphaTextureThreshold) {
      alpha = 0.0;
    }
  }
  // Dithered transparency.
  if (alpha < c_alphaDitherMax && rand_gradient_noise(in_fragCoord.xy) > alpha) {
    discard;
  }

  Geometry geo;
  geo.tags = floatBitsToUint(in_data.x);

  // Output color and roughness.
  const f32v4 colorRough = texture(u_texColorRough, in_texcoord);
  geo.color              = colorRough.rgb;
  geo.roughness          = colorRough.a;

  // Output world normal.
  if (s_normalMap) {
    const f32v4 normalEmissiveSample = texture(u_texNormalEmissive, in_texcoord);
    if (s_emissive && tag_is_set(geo.tags, tag_emit_bit)) {
      geo.emissive = normalEmissiveSample.a;
    } else {
      geo.emissive = 0;
    }
    geo.normal = texture_normal(normalEmissiveSample.xyz, in_worldNormal, in_worldTangent);
  } else {
    geo.emissive = 0;
    geo.normal   = in_worldNormal;
  }

  const GeometryEncoded encoded = geometry_encode(geo);
  out_data0                     = encoded.data0;
  out_data1                     = encoded.data1;
}
