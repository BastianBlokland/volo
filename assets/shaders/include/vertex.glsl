#ifndef INCLUDE_VERTEX
#define INCLUDE_VERTEX

#include "types.glsl"

struct VertexPacked {
  f16v4 data1; // x, y, z position
  f16v4 data2; // x, y texcoord
  f16v4 data3; // x, y, z normal
  f16v4 data4; // x, y, z tangent, w tangent handedness
};

struct Vertex {
  f32v3 position;
  f32v2 texcoord;
  f32v3 normal;
  f32v4 tangent;
};

#define vert_unpack(_VERT_)                                                                        \
  Vertex(                                                                                          \
      f32v4((_VERT_).data1).xyz,                                                                   \
      f32v2(f32v4((_VERT_).data2).xy),                                                             \
      f32v4((_VERT_).data3).xyz,                                                                   \
      f32v4((_VERT_).data4))

#endif // INCLUDE_VERTEX
