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

bind_global_img(3) uniform sampler2D u_texFogBuffer;

bind_draw_img(0) uniform sampler2D u_texTerrainHeight;

bind_instance_data(0) readonly uniform Instance { MinimapData u_instance; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v2 terrainCoord  = in_texcoord; // TODO: Compute proper terrain texcoord.
  const f32   terrainHeight = texture(u_texTerrainHeight, terrainCoord).r;
  const f32   fog           = texture(u_texFogBuffer, terrainCoord).r;
  const f32   alpha         = u_instance.data2.x;

  const f32 heightStepFrac = round(terrainHeight * c_terrainHeightSteps) * c_terrainHeightStepsInv;

  // Basic shading to indicate (banded) elevation, like a topographic map.
  f32v3 color = mix(c_terrainColor[0], c_terrainColor[1], heightStepFrac);

  // Fog of war.
  color *= mix(0.25, 1.0, fog);

  out_color = f32v4(color, alpha);
}
