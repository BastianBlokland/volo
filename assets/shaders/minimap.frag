#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "minimap.glsl"

// NOTE: Colors in linear space.
const f32v3 c_terrainColor[] = {
    f32v3(0.58702, 0.44799, 0.2049),
    f32v3(0.42003, 0.23302, 0.1128),
};
const f32 c_terrainHeightSteps    = 6.0;
const f32 c_terrainHeightStepsInv = 1.0 / c_terrainHeightSteps;

bind_graphic_img(0) uniform sampler2D u_texTerrainHeight;
bind_graphic_img(1) uniform sampler2D u_texTerrainSplat;

bind_draw_data(0) readonly uniform Draw { MinimapData u_draw; };
bind_draw_img(0) uniform sampler2D u_texFogBuffer;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v2 terrainCoord  = in_texcoord; // TODO: Compute proper terrain texcoord.
  const f32   terrainHeight = texture(u_texTerrainHeight, terrainCoord).r;
  const f32v4 terrainSplat  = texture(u_texTerrainSplat, terrainCoord);
  const f32   fog           = texture(u_texFogBuffer, terrainCoord).r;

  f32v3     color = f32v3(0);
  const f32 alpha = u_draw.data2.x;

  // Add color based on the type of terrain.
  color += terrainSplat.r * c_terrainColor[0];
  color += terrainSplat.g * c_terrainColor[1];

  // Basic shading to indicate (banded) elevation, like a topographic map.
  color *= mix(0.5, 2.0, round(terrainHeight * c_terrainHeightSteps) * c_terrainHeightStepsInv);

  // Fog of war.
  color *= mix(0.25, 1.0, fog);

  out_color = f32v4(color, alpha);
}
