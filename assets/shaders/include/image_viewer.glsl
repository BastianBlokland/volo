#ifndef INCLUDE_IMAGE_VIEWER
#define INCLUDE_IMAGE_VIEWER

#include "types.glsl"

struct ImageData {
  u32 flags;
  u32 imageChannels;
  f32 lod;
  f32 layer;
  f32 exposure;
  f32 aspect;
};

#endif // INCLUDE_IMAGE_VIEWER
