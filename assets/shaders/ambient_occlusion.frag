#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "texture.glsl"

const u32 c_kernelSize = 16; // Needs to match the ao kernel size in rend_settings.h

struct AoData {
  f32   radius;
  f32   power;
  f32v4 kernel[c_kernelSize];
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoNormalTags;
bind_global(2) uniform sampler2D u_texGeoDepth;
bind_draw_data(0) readonly uniform Draw { AoData u_draw; };

bind_graphic(1) uniform sampler2D u_texRotNoise;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32 out_occlusion;

f32v3 clip_to_view_pos(const f32v3 clipPos) {
  const f32v4 v = u_global.projInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

f32v3 world_to_view_dir(const f32v3 worldDir) { return (u_global.view * f32v4(worldDir, 0)).xyz; }

f32v3 view_to_clip_pos(const f32v3 viewPos) {
  const f32v4 v = u_global.proj * f32v4(viewPos, 1);
  return v.xyz / v.w;
}

f32m3 kernel_rotation_matrix(const f32v3 surfaceNormal, const f32v3 randomNormal) {
  // Construct an orthogonal basis using the Gram–Schmidt process.
  const f32v3 tangent = normalize(randomNormal - surfaceNormal * dot(randomNormal, surfaceNormal));
  const f32v3 bitangent = cross(surfaceNormal, tangent);
  return f32m3(tangent, bitangent, surfaceNormal);
}

void main() {
  const f32v3 worldNormal = normal_tex_decode(texture(u_texGeoNormalTags, in_texcoord).xyz);
  const f32v3 viewNormal  = world_to_view_dir(worldNormal);

  const f32   depth   = texture(u_texGeoDepth, in_texcoord).r;
  const f32v3 clipPos = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 viewPos = clip_to_view_pos(clipPos);

  const f32v2 rotNoiseScale = u_global.resolution.xy / f32v2(textureSize(u_texRotNoise, 0));
  const f32v3 randNormal    = texture(u_texRotNoise, in_texcoord * rotNoiseScale).xyz * 2.0 - 1.0;

  const f32m3 rotMatrix = kernel_rotation_matrix(viewNormal, randNormal);

  f32 occlusion = 0.0;
  for (u32 i = 0; i != c_kernelSize; ++i) {
    const f32v3 kernelViewPos = viewPos + rotMatrix * u_draw.kernel[i].xyz;
    const f32v3 kernelClipPos = view_to_clip_pos(kernelViewPos);
    const f32v2 kernelCoord   = kernelClipPos.xy * 0.5 + 0.5;

    const f32   sampleDepth   = texture(u_texGeoDepth, kernelCoord).r;
    const f32v3 sampleViewPos = clip_to_view_pos(f32v3(kernelClipPos.xy, sampleDepth));

    const f32 fade = smoothstep(0.0, 1.0, u_draw.radius / abs(viewPos.z - sampleViewPos.z));
    occlusion += f32(sampleViewPos.z < kernelViewPos.z) * fade;
  }
  out_occlusion = pow(1.0 - occlusion / f32(c_kernelSize), u_draw.power);
}
