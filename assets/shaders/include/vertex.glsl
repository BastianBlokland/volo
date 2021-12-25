#ifndef INCLUDE_VERTEX
#define INCLUDE_VERTEX

#include "types.glsl"

struct VertexPacked {
  f16_vec4 position; // x, y, z position
  f16_vec4 texcoord; // x, y texcoord1
};

#define vert_position(_VERT_) f32_vec4((_VERT_).position).xyz
#define vert_texcoord(_VERT_) f32_vec4((_VERT_).texcoord).xy

#endif // INCLUDE_VERTEX
