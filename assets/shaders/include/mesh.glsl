#ifndef INCLUDE_MESH
#define INCLUDE_MESH

#include "binding.glsl"
#include "types.glsl"

struct VertexPacked {
  f16_vec4 position; // x, y, z position
  f16_vec4 texcoord; // x, y texcoord1
};

bind_graphic_align(0) readonly buffer Mesh { VertexPacked[] vertices; };

f32_vec3 mesh_position(const u32 index) { return f32_vec4(vertices[index].position).xyz; }
f32_vec2 mesh_texcoord(const u32 index) { return f32_vec4(vertices[index].texcoord).xy; }

#endif // INCLUDE_MESH
