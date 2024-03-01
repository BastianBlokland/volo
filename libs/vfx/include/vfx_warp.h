#pragma once
#include "geo_vector.h"

/**
 * 3x3 transformation matrix for 2D warps.
 * NOTE: w component of columns is unused.
 */
typedef struct {
  GeoVector columns[3];
} VfxWarp;

typedef struct {
  f32 x, y;
} VfxWarpVec;

#define vfx_warp_vec_fmt(_VEC_) fmt_list_lit(fmt_float((_VEC_).x), fmt_float((_VEC_).y))

VfxWarpVec vfx_warp_vec_add(VfxWarpVec, VfxWarpVec);
VfxWarpVec vfx_warp_vec_sub(VfxWarpVec, VfxWarpVec);
VfxWarpVec vfx_warp_vec_mul(VfxWarpVec, f32 scalar);
bool       vfx_warp_vec_eq(VfxWarpVec, VfxWarpVec, f32 threshold);

VfxWarpVec vfx_warp_apply(const VfxWarp*, VfxWarpVec point);
VfxWarp    vfx_warp_invert(const VfxWarp*);
VfxWarp    vfx_warp_ident();
VfxWarp    vfx_warp_to_points(const VfxWarpVec points[PARAM_ARRAY_SIZE(4)]);
VfxWarp    vfx_warp_from_points(const VfxWarpVec points[PARAM_ARRAY_SIZE(4)]);
