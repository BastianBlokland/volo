#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "geometry.glsl"
#include "rand.glsl"
#include "tags.glsl"

const f32 c_alphaTextureThreshold = 0.2;
const f32 c_alphaDitherMax        = 0.99;

bind_spec(0) const bool s_normalMap    = false;
bind_spec(1) const bool s_alphaTexture = false;

bind_graphic_img(0) uniform sampler2D u_texColorRough;
bind_graphic_img(1) uniform sampler2D u_texNormal;
bind_graphic_img(2) uniform sampler2D u_texAlpha;

bind_internal(0) in f32v3 in_worldNormal;  // NOTE: non-normalized
bind_internal(1) in f32v4 in_worldTangent; // NOTE: non-normalized
bind_internal(2) in f32v2 in_texcoord;
bind_internal(3) in flat f32v4 in_data;

bind_internal(0) out f32v4 out_data0;
bind_internal(1) out f32v4 out_data1;

void main() {
  f32 alpha = in_data.y;
  if (s_alphaTexture) {
    if (texture(u_texAlpha, in_texcoord).r < c_alphaTextureThreshold) {
      alpha = 0.0;
    }
  }
  // Dithered transparency.
  if (alpha < c_alphaDitherMax && rand_gradient_noise(in_fragCoord.xy) > alpha) {
    discard;
  }

  const f32v4 colorRough = texture(u_texColorRough, in_texcoord);
  const f32v3 color      = colorRough.rgb;
  const f32   roughness  = colorRough.a;
  const u32   tags       = floatBitsToUint(in_data.x);

  f32v3 normal;
  if (s_normalMap) {
    normal = texture_normal(u_texNormal, in_texcoord, in_worldNormal, in_worldTangent);
  } else {
    normal = in_worldNormal;
  }

  const GeoSurfaceEncoded encoded = geo_surface_encode(color, roughness, normal, tags);
  out_data0                       = encoded.data0;
  out_data1                       = encoded.data1;
}
