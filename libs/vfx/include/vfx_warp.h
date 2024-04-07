#pragma once
#include "geo_vector.h"

typedef struct {
  f32 x, y;
} VfxWarpVec;

/**
 * 3x3 transformation matrix for 2D warps (including projective warps).
 * NOTE: w component of columns is unused.
 */
typedef struct {
  GeoVector columns[3];
} VfxWarpMatrix;

#define vfx_warp_vec_fmt(_VEC_) fmt_list_lit(fmt_float((_VEC_).x), fmt_float((_VEC_).y))

VfxWarpVec vfx_warp_vec_add(VfxWarpVec, VfxWarpVec);
VfxWarpVec vfx_warp_vec_sub(VfxWarpVec, VfxWarpVec);
VfxWarpVec vfx_warp_vec_mul(VfxWarpVec, f32 scalar);
VfxWarpVec vfx_warp_vec_div(VfxWarpVec, f32 scalar);
VfxWarpVec vfx_warp_vec_min(VfxWarpVec, VfxWarpVec);
VfxWarpVec vfx_warp_vec_max(VfxWarpVec, VfxWarpVec);
VfxWarpVec vfx_warp_vec_mid(VfxWarpVec, VfxWarpVec);
VfxWarpVec vfx_warp_vec_project_forward(VfxWarpVec, VfxWarpVec normal);
VfxWarpVec vfx_warp_vec_abs(VfxWarpVec);
bool       vfx_warp_vec_eq(VfxWarpVec, VfxWarpVec, f32 threshold);
f32        vfx_warp_vec_dot(VfxWarpVec, VfxWarpVec);
f32        vfx_warp_vec_cross(VfxWarpVec, VfxWarpVec);
VfxWarpVec vfx_warp_vec_perpendicular(VfxWarpVec);

VfxWarpVec vfx_warp_bounds(const VfxWarpVec points[], u32 pointCount, VfxWarpVec center);
bool       vfx_warp_is_convex(const VfxWarpVec points[], u32 pointCount);

VfxWarpVec    vfx_warp_matrix_apply(const VfxWarpMatrix*, VfxWarpVec point);
VfxWarpMatrix vfx_warp_matrix_invert(const VfxWarpMatrix*);
VfxWarpMatrix vfx_warp_matrix_ident(void);
VfxWarpMatrix vfx_warp_matrix_offset_scale(VfxWarpVec offset, VfxWarpVec scale);
VfxWarpMatrix vfx_warp_matrix_to_points(const VfxWarpVec points[PARAM_ARRAY_SIZE(4)]);
VfxWarpMatrix vfx_warp_matrix_from_points(const VfxWarpVec points[PARAM_ARRAY_SIZE(4)]);
