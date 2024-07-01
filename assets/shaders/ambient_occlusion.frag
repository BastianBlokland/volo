#include "binding.glsl"
#include "geometry.glsl"
#include "global.glsl"

const u32 c_kernelSize = 16; // Needs to match the ao kernel size in rend_settings.h

struct AoData {
  f32   radius;
  f32   power;
  f32v4 kernel[c_kernelSize];
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(0) uniform sampler2D u_texGeoData1;
bind_global_img(1) uniform sampler2D u_texGeoDepth;
bind_draw_data(0) readonly uniform Draw { AoData u_draw; };

bind_graphic_img(0) uniform sampler2D u_texRotNoise;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32 out_occlusion;

/**
 * Generate a random normal based on a repeating noise texture.
 * NOTE: Not actually normalized.
 */
f32v3 random_normal() {
  const f32v2 noiseScale = u_global.resolution.xy / f32v2(textureSize(u_texRotNoise, 0));
  return texture(u_texRotNoise, in_texcoord * noiseScale).xyz * 2.0 - 1.0;
}

/**
 * Construct a rotation matrix to orient samples along the surface normal.
 */
f32m3 kernel_rotation_matrix(const f32v3 surfaceNormal, const f32v3 randomNormal) {
  // Construct an orthogonal basis using the Gram–Schmidt process.
  const f32v3 tangent = normalize(randomNormal - surfaceNormal * dot(randomNormal, surfaceNormal));
  const f32v3 bitangent = cross(surfaceNormal, tangent);
  return f32m3(tangent, bitangent, surfaceNormal);
}

void main() {
  const f32v4 geoData1    = texture(u_texGeoData1, in_texcoord);
  const f32v3 worldNormal = geometry_decode_normal(geoData1);
  const f32v3 viewNormal  = world_to_view_dir(u_global, worldNormal);

  const f32   depth   = texture(u_texGeoDepth, in_texcoord).r;
  const f32v3 clipPos = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 viewPos = clip_to_view_pos(u_global, clipPos);

  const f32m3 rotMatrix = kernel_rotation_matrix(viewNormal, random_normal());

  f32 occlusion = 0.0;
  for (u32 i = 0; i < c_kernelSize; ++i) {
    /**
     * Get the kernel position in view-space, oriented along the surface normal.
     */
    const f32v3 kernelViewPos = viewPos + rotMatrix * u_draw.kernel[i].xyz;
    const f32v3 kernelClipPos = view_to_clip_pos(u_global, kernelViewPos);
    const f32v2 kernelCoord   = kernelClipPos.xy * 0.5 + 0.5;

    /**
     * Sample the depth at the kernel position and compute the hit view-pace position.
     */
    const f32   hitDepth   = texture(u_texGeoDepth, kernelCoord).r;
    const f32v3 hitViewPos = clip_to_view_pos(u_global, f32v3(kernelClipPos.xy, hitDepth));

    /**
     * The sample is occluded if the hit z is closer then the sample z.
     */
    const f32 fade = smoothstep(0.0, 1.0, u_draw.radius / abs(viewPos.z - hitViewPos.z));
    occlusion += f32(hitViewPos.z < kernelViewPos.z) * fade;
  }

  out_occlusion = pow(1.0 - occlusion / f32(c_kernelSize), u_draw.power);
}
