#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "light.glsl"
#include "quat.glsl"
#include "texture.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_graphic(1) uniform sampler2D u_texHeightMap;
bind_graphic(2) uniform sampler2D u_texDiffuse;

bind_internal(0) in flat f32 in_size;
bind_internal(1) in flat f32 in_heightScale;
bind_internal(2) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 heightmap_normal(const f32v2 uv, const f32 size, const f32 heightScale) {
  /**
   * Calculate the normal by taking samples around this location and normalizing the deltas.
   */
  const f32 hLeft  = textureOffset(u_texHeightMap, uv, i32v2(-1, 0)).r * heightScale;
  const f32 hRight = textureOffset(u_texHeightMap, uv, i32v2(1, 0)).r * heightScale;
  const f32 hDown  = textureOffset(u_texHeightMap, uv, i32v2(0, -1)).r * heightScale;
  const f32 hUp    = textureOffset(u_texHeightMap, uv, i32v2(0, 1)).r * heightScale;

  // NOTE: Assumes that the height map is a square texture.
  const f32 unitSize = 1.0 / textureSize(u_texHeightMap, 0).x * size;

  return normalize(f32v3(hLeft - hRight, unitSize * 2, hDown - hUp));
}

void main() {
  const f32v4 diffuse = texture(u_texDiffuse, in_texcoord);
  const f32v3 viewDir = quat_rotate(u_global.camRotation, f32v3(0, 0, 1));
  const f32v3 normal  = heightmap_normal(in_texcoord, in_size, in_heightScale);

  const Shading shading = light_shade_blingphong(normal, viewDir);

  out_color = light_color(shading, diffuse);
}
