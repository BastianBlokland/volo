#include "binding.glsl"
#include "geometry.glsl"
#include "instance.glsl"
#include "noise.glsl"
#include "tag.glsl"

const f32 c_alphaTextureThreshold = 0.2;
const f32 c_alphaDitherMax        = 0.99;

bind_spec(0) const bool s_normalMap   = false;
bind_spec(1) const bool s_alphaMap    = false;
bind_spec(2) const bool s_emissiveMap = false;
bind_spec(3) const bool s_maskMap     = false;

bind_graphic_img(0) uniform sampler2D u_texColor;
bind_graphic_img(1) uniform sampler2D u_texRough;
bind_graphic_img(2) uniform sampler2D u_texMetal;
bind_graphic_img(3) uniform sampler2D u_texNormal;
bind_graphic_img(4) uniform sampler2D u_texEmissive;
bind_graphic_img(5) uniform sampler2D u_texAlpha;
bind_graphic_img(6) uniform sampler2D u_texMask;

bind_internal(0) in f32v3 in_worldNormal;  // NOTE: non-normalized
bind_internal(1) in f32v4 in_worldTangent; // NOTE: non-normalized
bind_internal(2) in f32v2 in_texcoord;
bind_internal(3) in flat f32v4 in_data; // x tag bits, y color, z emissive

bind_internal(0) out f32v4 out_base;
bind_internal(1) out f32v2 out_normal;
bind_internal(2) out f32v2 out_attribute;
bind_internal(3) out f32v3 out_emissive;

void main() {
  f32v4 color = instance_color(in_data);
  if (s_alphaMap) {
    if (texture(u_texAlpha, in_texcoord).r < c_alphaTextureThreshold) {
      color.a = 0.0;
    }
  }
  // Dithered transparency.
  if (color.a < c_alphaDitherMax && noise_gradient_f32v2(in_fragCoord.xy) > color.a) {
    discard;
  }

  // Output base.
  GeoBase base;
  base.tags  = floatBitsToUint(in_data.x);
  base.color = texture(u_texColor, in_texcoord).rgb;
  if (s_maskMap) {
    const f32 mask = texture(u_texMask, in_texcoord).r;
    base.color     = base.color * mix(f32v3(1, 1, 1), color.rgb, mask);
  } else {
    base.color = base.color * color.rgb;
  }
  out_base = geo_base_encode(base);

  // Output attributes.
  GeoAttribute attr;
  attr.roughness = texture(u_texRough, in_texcoord).r;
  attr.metalness = texture(u_texMetal, in_texcoord).r;
  out_attribute  = geo_attr_encode(attr);

  // Output normal.
  if (s_normalMap) {
    const f32v3 normalSample = texture(u_texNormal, in_texcoord).xyz;
    out_normal = geo_normal_encode(texture_normal(normalSample, in_worldNormal, in_worldTangent));
  } else {
    out_normal = geo_normal_encode(in_worldNormal);
  }

  // Output emissive.
  const f32v4 emissive = instance_emissive(in_data);
  if (s_emissiveMap) {
    out_emissive = emissive.rgb * texture(u_texEmissive, in_texcoord).rgb * emissive.a;
  } else {
    out_emissive = emissive.rgb * emissive.a;
  }
}
