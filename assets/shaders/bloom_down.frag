#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "tag.glsl"
#include "texture.glsl"

bind_global_img(0) uniform sampler2D u_texInput;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v3 out_color;

void main() {
  /**
   * Down-sampling filter kernel from Call Of Duty, presented by SledgeHammer at ACM Siggraph 2014.
   * http://advances.realtimerendering.com/s2014/
   *
   * Implementation based on: https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
   */

  const f32v2 inputTexelSize = 1.0 / f32v2(textureSize(u_texInput, 0));
  const f32   sX             = inputTexelSize.x;
  const f32   sY             = inputTexelSize.y;

  /**
   * Take 13 samples around current texel 'e':
   * a - b - c
   * - j - k -
   * d - e - f
   * - l - m -
   * g - h - i
   */
  const f32v3 a = texture(u_texInput, f32v2(in_texcoord.x - 2 * sX, in_texcoord.y + 2 * sY)).rgb;
  const f32v3 b = texture(u_texInput, f32v2(in_texcoord.x, in_texcoord.y + 2 * sY)).rgb;
  const f32v3 c = texture(u_texInput, f32v2(in_texcoord.x + 2 * sX, in_texcoord.y + 2 * sY)).rgb;

  const f32v3 d = texture(u_texInput, f32v2(in_texcoord.x - 2 * sX, in_texcoord.y)).rgb;
  const f32v3 e = texture(u_texInput, f32v2(in_texcoord.x, in_texcoord.y)).rgb;
  const f32v3 f = texture(u_texInput, f32v2(in_texcoord.x + 2 * sX, in_texcoord.y)).rgb;

  const f32v3 g = texture(u_texInput, f32v2(in_texcoord.x - 2 * sX, in_texcoord.y - 2 * sY)).rgb;
  const f32v3 h = texture(u_texInput, f32v2(in_texcoord.x, in_texcoord.y - 2 * sY)).rgb;
  const f32v3 i = texture(u_texInput, f32v2(in_texcoord.x + 2 * sX, in_texcoord.y - 2 * sY)).rgb;

  const f32v3 j = texture(u_texInput, f32v2(in_texcoord.x - sX, in_texcoord.y + sY)).rgb;
  const f32v3 k = texture(u_texInput, f32v2(in_texcoord.x + sX, in_texcoord.y + sY)).rgb;
  const f32v3 l = texture(u_texInput, f32v2(in_texcoord.x - sX, in_texcoord.y - sY)).rgb;
  const f32v3 m = texture(u_texInput, f32v2(in_texcoord.x + sX, in_texcoord.y - sY)).rgb;

  /**
   * Apply weighted distribution:
   * 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
   * a,b,d,e * 0.125
   * b,c,e,f * 0.125
   * d,e,g,h * 0.125
   * e,f,h,i * 0.125
   * j,k,l,m * 0.5
   *
   * This shows 5 square areas that are being sampled. But some of them overlap, so to have an
   * energy preserving downsample we need to make some adjustments. The weights are distributed, so
   * that the sum of j,k,l,m (e.g.) contribute 0.5 to the final color output.
   *
   * The code below is written to effectively yield this sum.
   * We get: 0.125*5 + 0.03125*4 + 0.0625*4 = 1
   */
  out_color = e * 0.125;
  out_color += (a + c + g + i) * 0.03125;
  out_color += (b + d + f + h) * 0.0625;
  out_color += (j + k + l + m) * 0.125;
}
