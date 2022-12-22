#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "terrain.glsl"
#include "vertex.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_draw_data(0) readonly uniform Draw { TerrainData u_terrain; };
bind_instance_data(0) readonly uniform Instance {
  TerrainPatchData[c_terrainMaxPatches] u_patches;
};

bind_graphic(1) uniform sampler2D u_texHeight;

f32 heightmap_sample(const f32v2 uv, const f32 scale) { return texture(u_texHeight, uv).r * scale; }

void main() {
  const Vertex           vert      = vert_unpack(u_vertices[in_vertexIndex]);
  const TerrainPatchData patchData = u_patches[in_instanceIndex];

  const f32v2 uv     = f32v2(patchData.texU, patchData.texV) + vert.texcoord * u_terrain.patchScale;
  const f32   height = heightmap_sample(uv, u_terrain.heightScale);
  const f32v3 localPos = vert.position * u_terrain.patchScale * u_terrain.size;
  const f32v3 patchPos = f32v3(patchData.posX, height, patchData.posZ);

  out_vertexPosition = u_global.viewProj * f32v4(patchPos + localPos, 1);
}
