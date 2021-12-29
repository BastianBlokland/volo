#ifndef INCLUDE_VERTEX
#define INCLUDE_VERTEX

#include "types.glsl"

struct VertexData {
  f16_vec4 position; // x, y, z position
  f16_vec4 texcoord; // x, y texcoord1
  f16_vec4 normal;   // x, y, z normal
};

#define vert_position(_VERT_) f32_vec4((_VERT_).position).xyz
#define vert_texcoord(_VERT_) f32_vec4((_VERT_).texcoord).xy
#define vert_normal(_VERT_) f32_vec4((_VERT_).normal).xyz

#endif // INCLUDE_VERTEX
