#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "texture.glsl"

const u32 c_kernelSize = 16; // Needs to match the maximum in rend_painter.c
const f32 c_sampleBias = -0.05;

struct AoData {
  f32   radius;
  f32v4 kernel[c_kernelSize];
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoDepth;
bind_draw_data(0) readonly uniform Draw { AoData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32 out_occlusion;

f32v3 clip_to_view_pos(const f32v3 clipPos) {
  const f32v4 v = u_global.projInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

f32v3 view_to_clip_pos(const f32v3 viewPos) {
  const f32v4 v = u_global.proj * f32v4(viewPos, 1);
  return v.xyz / v.w;
}

void main() {
  const f32   depth   = texture(u_texGeoDepth, in_texcoord).r;
  const f32v3 clipPos = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 viewPos = clip_to_view_pos(clipPos);

  f32 occlusion = 0.0;
  for (u32 i = 0; i != c_kernelSize; ++i) {
    const f32v3 kernelViewPos = viewPos + u_draw.kernel[i].xyz * u_draw.radius;
    const f32v3 kernelClipPos = view_to_clip_pos(kernelViewPos);
    const f32v2 kernelCoord   = kernelClipPos.xy * 0.5 + 0.5;

    const f32   sampleDepth   = texture(u_texGeoDepth, kernelCoord).r;
    const f32v3 sampleViewPos = clip_to_view_pos(f32v3(kernelClipPos.xy, sampleDepth));

    const f32 fade = smoothstep(0.0, 1.0, u_draw.radius / abs(viewPos.z - sampleViewPos.z));
    occlusion += f32(sampleViewPos.z < kernelViewPos.z + c_sampleBias) * fade;
  }
  out_occlusion = 1.0 - occlusion / f32(c_kernelSize);
}
