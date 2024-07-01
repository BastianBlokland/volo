#include "binding.glsl"
#include "texture.glsl"

bind_spec(0) const bool s_horizontal = true;

const u32 c_sampleCount                  = 5;
const f32 c_kernelWeights[c_sampleCount] = {
    0.227027,
    0.1945946,
    0.1216216,
    0.054054,
    0.016216,
};

struct BlurData {
  f32 sampleScale;
};

bind_global_img(0) uniform sampler2D u_texInput;
bind_draw_data(0) readonly uniform Draw { BlurData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v2 sampleSize = 1.0 / textureSize(u_texInput, 0) * u_draw.sampleScale;
  f32v4       res        = texture(u_texInput, in_texcoord) * c_kernelWeights[0];
  for (i32 i = 1; i < c_sampleCount; ++i) {
    if (s_horizontal) {
      res += texture(u_texInput, in_texcoord + f32v2(sampleSize.x * i, 0)) * c_kernelWeights[i];
      res += texture(u_texInput, in_texcoord - f32v2(sampleSize.x * i, 0)) * c_kernelWeights[i];
    } else {
      res += texture(u_texInput, in_texcoord + f32v2(0, sampleSize.y * i)) * c_kernelWeights[i];
      res += texture(u_texInput, in_texcoord - f32v2(0, sampleSize.y * i)) * c_kernelWeights[i];
    }
  }
  out_color = res;
}
