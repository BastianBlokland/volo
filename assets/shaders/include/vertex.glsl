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

struct VertexSkinnedPacked {
  f16v4 data1; // x, y, z position
  f16v4 data2; // x, y texcoord
  f16v4 data3; // x, y, z normal
  f16v4 data4; // x, y, z tangent, w tangent handedness
  u16v4 data5; // x jointIndex0, y jointIndex1, z jointIndex2, w jointIndex3
  f16v4 data6; // x jointWeight0, y jointWeight1, z jointWeight2, w jointWeight3
};

struct VertexSkinned {
  f32v3 position;
  f32v2 texcoord;
  f32v3 normal;
  f32v4 tangent;
  u32v4 jointIndices;
  f32v4 jointWeights;
};

#define vert_skinned_unpack(_VERT_)                                                                \
  VertexSkinned(                                                                                   \
      f32v4((_VERT_).data1).xyz,                                                                   \
      f32v2(f32v4((_VERT_).data2).xy),                                                             \
      f32v4((_VERT_).data3).xyz,                                                                   \
      f32v4((_VERT_).data4),                                                                       \
      u32v4((_VERT_).data5),                                                                       \
      f32v4((_VERT_).data6))

#endif // INCLUDE_VERTEX
