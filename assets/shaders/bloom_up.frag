#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "tag.glsl"
#include "texture.glsl"

struct BloomData {
  f32 filterRadius;
};

bind_global_img(0) uniform sampler2D u_texInput;
bind_draw_data(0) readonly uniform Draw { BloomData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v3 out_color;

void main() {
  /**
   * Up-sampling filter kernel from Call Of Duty, presented by SledgeHammer at ACM Siggraph 2014.
   * http://advances.realtimerendering.com/s2014/
   *
   * Implementation based on: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
   */

  /**
   * The filter kernel is applied with a radius, specified in texture coordinates, so that the
   * radius will vary across mip resolutions.
   */
  const f32 sX = u_draw.filterRadius;
  const f32 sY = u_draw.filterRadius;

  /**
   * Take 9 samples around current texel 'e':
   * a - b - c
   * d - e - f
   * g - h - i
   */
  const f32v3 a = texture(u_texInput, f32v2(in_texcoord.x - sX, in_texcoord.y + sY)).rgb;
  const f32v3 b = texture(u_texInput, f32v2(in_texcoord.x, in_texcoord.y + sY)).rgb;
  const f32v3 c = texture(u_texInput, f32v2(in_texcoord.x + sX, in_texcoord.y + sY)).rgb;

  const f32v3 d = texture(u_texInput, f32v2(in_texcoord.x - sX, in_texcoord.y)).rgb;
  const f32v3 e = texture(u_texInput, f32v2(in_texcoord.x, in_texcoord.y)).rgb;
  const f32v3 f = texture(u_texInput, f32v2(in_texcoord.x + sX, in_texcoord.y)).rgb;

  const f32v3 g = texture(u_texInput, f32v2(in_texcoord.x - sX, in_texcoord.y - sY)).rgb;
  const f32v3 h = texture(u_texInput, f32v2(in_texcoord.x, in_texcoord.y - sY)).rgb;
  const f32v3 i = texture(u_texInput, f32v2(in_texcoord.x + sX, in_texcoord.y - sY)).rgb;

  /**
   * Apply weighted distribution, by using a 3x3 tent filter:
   *  1   | 1 2 1 |
   * -- * | 2 4 2 |
   * 16   | 1 2 1 |
   */
  out_color = e * 4.0;
  out_color += (b + d + f + h) * 2.0;
  out_color += (a + c + g + i);
  out_color *= 1.0 / 16.0;
}
