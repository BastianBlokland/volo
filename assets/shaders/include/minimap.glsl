#ifndef INCLUDE_MINIMAP
#define INCLUDE_MINIMAP

struct MinimapData {
  f32v4 data1; // x, y: position, z, w: size.
  f32v4 data2; // x: alpha, y, z, w: unused.
};

#endif // INCLUDE_MINIMAP
