#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "vfx_warp.h"

VfxWarpVec vfx_warp_vec_add(const VfxWarpVec a, const VfxWarpVec b) {
  return (VfxWarpVec){.x = a.x + b.x, .y = a.y + b.y};
}

VfxWarpVec vfx_warp_vec_sub(const VfxWarpVec a, const VfxWarpVec b) {
  return (VfxWarpVec){.x = a.x - b.x, .y = a.y - b.y};
}

VfxWarpVec vfx_warp_vec_mul(const VfxWarpVec a, const f32 scalar) {
  return (VfxWarpVec){.x = a.x * scalar, .y = a.y * scalar};
}

VfxWarpVec vfx_warp_vec_div(const VfxWarpVec a, const f32 scalar) {
  return (VfxWarpVec){.x = a.x / scalar, .y = a.y / scalar};
}

VfxWarpVec vfx_warp_vec_min(const VfxWarpVec a, const VfxWarpVec b) {
  return (VfxWarpVec){math_min(a.x, b.x), math_min(a.y, b.y)};
}

VfxWarpVec vfx_warp_vec_max(const VfxWarpVec a, const VfxWarpVec b) {
  return (VfxWarpVec){math_max(a.x, b.x), math_max(a.y, b.y)};
}

VfxWarpVec vfx_warp_vec_mid(const VfxWarpVec a, const VfxWarpVec b) {
  return vfx_warp_vec_mul(vfx_warp_vec_add(a, b), 0.5f);
}

VfxWarpVec vfx_warp_vec_project_forward(const VfxWarpVec v, const VfxWarpVec normal) {
  const f32 nrmSqrMag = vfx_warp_vec_dot(normal, normal);
  if (nrmSqrMag <= f32_epsilon) {
    return (VfxWarpVec){0.0f, 0.0f};
  }
  // NOTE: Taking the absolute here makes sure we only project forward and not backward.
  return vfx_warp_vec_mul(normal, math_abs(vfx_warp_vec_dot(v, normal)) / nrmSqrMag);
}

VfxWarpVec vfx_warp_vec_abs(const VfxWarpVec v) {
  return (VfxWarpVec){math_abs(v.x), math_abs(v.y)};
}

bool vfx_warp_vec_eq(const VfxWarpVec a, const VfxWarpVec b, const f32 threshold) {
  return math_abs(a.x - b.x) <= threshold && math_abs(a.y - b.y) <= threshold;
}

f32 vfx_warp_vec_dot(const VfxWarpVec a, const VfxWarpVec b) { return a.x * b.x + a.y * b.y; }
f32 vfx_warp_vec_cross(const VfxWarpVec a, const VfxWarpVec b) { return a.x * b.y - a.y * b.x; }
VfxWarpVec vfx_warp_vec_perpendicular(const VfxWarpVec v) { return (VfxWarpVec){v.y, -v.x}; }

VfxWarpVec
vfx_warp_bounds(const VfxWarpVec points[], const u32 pointCount, const VfxWarpVec center) {
  diag_assert(pointCount);

  VfxWarpVec halfSize = vfx_warp_vec_abs(vfx_warp_vec_sub(points[0], center));
  for (u32 i = 1; i != pointCount; ++i) {
    const VfxWarpVec toPoint = vfx_warp_vec_abs(vfx_warp_vec_sub(points[i], center));
    halfSize                 = vfx_warp_vec_max(halfSize, toPoint);
  }
  return vfx_warp_vec_mul(halfSize, 2);
}

bool vfx_warp_is_convex(const VfxWarpVec points[], const u32 pointCount) {
  for (u32 i = 0; i != pointCount; ++i) {
    const VfxWarpVec a = points[i];
    const VfxWarpVec b = points[(i + 1) % pointCount];
    const VfxWarpVec c = points[(i + 2) % pointCount];
    if (vfx_warp_vec_cross(vfx_warp_vec_sub(b, a), vfx_warp_vec_sub(c, a)) < 0) {
      return false;
    }
  }
  return true;
}

VfxWarpVec vfx_warp_matrix_apply(const VfxWarpMatrix* warp, const VfxWarpVec p) {
  const f32 w = 1.0f / (warp->columns[0].z * p.x + warp->columns[1].z * p.y + warp->columns[2].z);
  const f32 x = warp->columns[0].x * p.x + warp->columns[1].x * p.y + warp->columns[2].x;
  const f32 y = warp->columns[0].y * p.x + warp->columns[1].y * p.y + warp->columns[2].y;
  return (VfxWarpVec){.x = x * w, .y = y * w};
}

VfxWarpMatrix vfx_warp_matrix_invert(const VfxWarpMatrix* w) {
  const f32 d0 = w->columns[1].y * w->columns[2].z - w->columns[2].y * w->columns[1].z;
  const f32 d1 = w->columns[2].x * w->columns[1].z - w->columns[1].x * w->columns[2].z;
  const f32 d2 = w->columns[1].x * w->columns[2].y - w->columns[2].x * w->columns[1].y;
  const f32 d  = w->columns[0].x * d0 + w->columns[0].y * d1 + w->columns[0].z * d2;
  diag_assert_msg(math_abs(d) > 0, "Singular vfx warp matrix");
  const f32 dInv = 1.0f / d;
  return (VfxWarpMatrix){
      .columns = {
          {
              d0 * dInv,
              (w->columns[2].y * w->columns[0].z - w->columns[0].y * w->columns[2].z) * dInv,
              (w->columns[0].y * w->columns[1].z - w->columns[1].y * w->columns[0].z) * dInv,
          },
          {
              d1 * dInv,
              (w->columns[0].x * w->columns[2].z - w->columns[2].x * w->columns[0].z) * dInv,
              (w->columns[1].x * w->columns[0].z - w->columns[0].x * w->columns[1].z) * dInv,
          },
          {
              d2 * dInv,
              (w->columns[2].x * w->columns[0].y - w->columns[0].x * w->columns[2].y) * dInv,
              (w->columns[0].x * w->columns[1].y - w->columns[1].x * w->columns[0].y) * dInv,
          },
      }};
}

VfxWarpMatrix vfx_warp_matrix_ident(void) {
  return (VfxWarpMatrix){
      .columns = {
          {1, 0, 0},
          {0, 1, 0},
          {0, 0, 1},
      }};
}

VfxWarpMatrix vfx_warp_matrix_offset_scale(const VfxWarpVec offset, const VfxWarpVec scale) {
  return (VfxWarpMatrix){
      .columns = {
          {scale.x, 0, 0},
          {0, scale.y, 0},
          {offset.x, offset.y, 1},
      }};
}

VfxWarpMatrix vfx_warp_matrix_to_points(const VfxWarpVec p[PARAM_ARRAY_SIZE(4)]) {
  const VfxWarpVec d = vfx_warp_vec_add(vfx_warp_vec_sub(p[0], p[1]), vfx_warp_vec_sub(p[2], p[3]));
  if (math_abs(d.x) < f32_epsilon && math_abs(d.y) < f32_epsilon) {
    // Affine transformation.
    const VfxWarpVec to1 = vfx_warp_vec_sub(p[1], p[0]);
    const VfxWarpVec to2 = vfx_warp_vec_sub(p[2], p[1]);
    return (VfxWarpMatrix){
        .columns = {
            {to1.x, to1.y, 0.0f},
            {to2.x, to2.y, 0.0f},
            {p[0].x, p[0].y, 1.0f},
        }};
  }
  const VfxWarpVec d1  = vfx_warp_vec_sub(p[1], p[2]);
  const VfxWarpVec d2  = vfx_warp_vec_sub(p[3], p[2]);
  const f32        den = d1.x * d2.y - d2.x * d1.y;
  diag_assert_msg(math_abs(den) > 0, "Singular vfx warp matrix");
  const f32        denInv = 1.0f / den;
  const f32        u      = (d.x * d2.y - d.y * d2.x) * denInv;
  const f32        v      = (d.y * d1.x - d.x * d1.y) * denInv;
  const VfxWarpVec to1    = vfx_warp_vec_sub(p[1], p[0]);
  const VfxWarpVec to3    = vfx_warp_vec_sub(p[3], p[0]);
  return (VfxWarpMatrix){
      .columns = {
          {to1.x + u * p[1].x, to1.y + u * p[1].y, u},
          {to3.x + v * p[3].x, to3.y + v * p[3].y, v},
          {p[0].x, p[0].y, 1.0f},
      }};
}

VfxWarpMatrix vfx_warp_matrix_from_points(const VfxWarpVec p[PARAM_ARRAY_SIZE(4)]) {
  diag_assert(vfx_warp_is_convex(p, 4));
  const VfxWarpMatrix w = vfx_warp_matrix_to_points(p);
  return vfx_warp_matrix_invert(&w);
}
