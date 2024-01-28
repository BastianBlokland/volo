#ifndef INCLUDE_MINIMAP
#define INCLUDE_MINIMAP

struct MinimapData {
  f32v4 data1; // x, y: position, z, w: size.
  f32v4 data2; // x: alpha, y: zoomInv, z, w: unused.
  f32v4 colorLow, colorHigh;
};

#endif // INCLUDE_MINIMAP
