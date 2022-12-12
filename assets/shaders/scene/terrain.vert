#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "vertex.glsl"

struct TerrainData {
  f32 size;
  f32 heightScale;
};

struct PatchData {
  f32v4 position; // x, y, z: position
};

const u32 c_maxPatches = 512;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_draw_data(0) readonly uniform Draw { TerrainData u_terrain; };
bind_instance_data(0) readonly uniform Instance { PatchData[c_maxPatches] u_patches; };

bind_graphic(1) uniform sampler2D u_texHeightMap;

bind_internal(0) out flat f32 out_size;
bind_internal(1) out flat f32 out_heightScale;
bind_internal(2) out f32v2 out_texcoord;

f32 heightmap_sample(const f32v2 uv, const f32 scale) {
  return texture(u_texHeightMap, uv).r * scale;
}

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32   height   = heightmap_sample(vert.texcoord, u_terrain.heightScale);
  const f32v3 localPos = f32v3(vert.position.x, vert.position.y + height, vert.position.z);
  const f32v3 patchPos = u_patches[in_instanceIndex].position.xyz;

  out_vertexPosition = u_global.viewProj * f32v4(patchPos + localPos, 1);
  out_size           = u_terrain.size;
  out_heightScale    = u_terrain.heightScale;
  out_texcoord       = vert.texcoord;
}
