#ifndef INCLUDE_VERTEX
#define INCLUDE_VERTEX

#include "types.glsl"

struct VertexPacked {
  f16v4 data1; // x, y, z position, w texcoord x
  f16v4 data2; // x, y, z normal , w texcoord y
  f16v4 data3; // x, y, z tangent, w tangent handedness
  u16v4 data4; // x jntIndexWeight0, y jntIndexWeight1, z jntIndexWeight2, w jntIndexWeight3,
};

struct Vertex {
  f32v3 position;
  f32v2 texcoord;
  f32v3 normal;
  f32v4 tangent;
  u32v4 jointIndices;
  f32v4 jointWeights;
};

#define vert_unpack(_VERT_)                                                                        \
  Vertex(                                                                                          \
      f32v4((_VERT_).data1).xyz,                                                                   \
      f32v2(f32v4((_VERT_).data1).w, f32v4((_VERT_).data2).w),                                     \
      f32v4((_VERT_).data2).xyz,                                                                   \
      f32v4((_VERT_).data3),                                                                       \
      u32v4(                                                                                       \
          u32v4((_VERT_).data4).x& u32(0xFF),                                                      \
          u32v4((_VERT_).data4).y& u32(0xFF),                                                      \
          u32v4((_VERT_).data4).z& u32(0xFF),                                                      \
          u32v4((_VERT_).data4).w& u32(0xFF)),                                                     \
      f32v4(                                                                                       \
          (u32v4((_VERT_).data4).x >> 8) / 256.0,                                                  \
          (u32v4((_VERT_).data4).y >> 8) / 256.0,                                                  \
          (u32v4((_VERT_).data4).z >> 8) / 256.0,                                                  \
          (u32v4((_VERT_).data4).w >> 8) / 256.0))

#endif // INCLUDE_VERTEX
