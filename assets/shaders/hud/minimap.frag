#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "minimap.glsl"

const f32 c_terrainHeightSteps    = 10.0;
const f32 c_terrainHeightStepsInv = 1.0 / c_terrainHeightSteps;

bind_global_img(3) uniform sampler2D u_texFogBuffer;

bind_draw_img(0) uniform sampler2D u_texTerrainHeight;

bind_instance_data(0) readonly uniform Instance { MinimapData u_instance; };

bind_internal(0) in f32v2 in_terrainCoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32 terrainHeight = texture(u_texTerrainHeight, in_terrainCoord).r;
  const f32 fog           = texture(u_texFogBuffer, in_terrainCoord).r;
  const f32 alpha         = u_instance.data2.x;

  const f32 heightStepFrac = round(terrainHeight * c_terrainHeightSteps) * c_terrainHeightStepsInv;

  // Basic shading to indicate (banded) elevation, like a topographic map.
  f32v3 color = mix(u_instance.colorLow.rgb, u_instance.colorHigh.rgb, heightStepFrac);

  // Fog of war.
  color *= mix(0.25, 1.0, fog);

  out_color = f32v4(color, alpha);
}
