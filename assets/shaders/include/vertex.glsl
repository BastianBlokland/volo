#ifndef INCLUDE_VERTEX
#define INCLUDE_VERTEX

#include "types.glsl"

struct VertexPacked {
  f16_vec4 data1; // x, y, z position
  f16_vec4 data2; // x, y texcoord
  f16_vec4 data3; // x, y, z normal
  f16_vec4 data4; // x, y, z tangent, w tangent handedness
};

struct Vertex {
  f32_vec3 position;
  f32_vec2 texcoord;
  f32_vec3 normal;
  f32_vec4 tangent;
};

#define vert_unpack(_VERT_)                                                                        \
  Vertex(                                                                                          \
      f32_vec4((_VERT_).data1).xyz,                                                                \
      f32_vec2(f32_vec4((_VERT_).data2).xy),                                                       \
      f32_vec4((_VERT_).data3).xyz,                                                                \
      f32_vec4((_VERT_).data4))

#endif // INCLUDE_VERTEX
