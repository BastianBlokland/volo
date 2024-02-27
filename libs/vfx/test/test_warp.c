#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_rng.h"
#include "geo_vector.h"

#define test_warp_vec_eq_threshold 1e-6f

/**
 * 2d transformation matrix 3x3.
 * NOTE: w component of columns is unused.
 */
typedef struct {
  GeoVector columns[3];
} VfxWarp;

typedef struct {
  f32 x, y;
} VfxWarpVec;

#define vfx_warp_vec_fmt(_VEC_) fmt_list_lit(fmt_float((_VEC_).x), fmt_float((_VEC_).y))

static VfxWarpVec vfx_warp_vec_add(const VfxWarpVec a, const VfxWarpVec b) {
  return (VfxWarpVec){.x = a.x + b.x, .y = a.y + b.y};
}

static VfxWarpVec vfx_warp_vec_sub(const VfxWarpVec a, const VfxWarpVec b) {
  return (VfxWarpVec){.x = a.x - b.x, .y = a.y - b.y};
}

static VfxWarpVec vfx_warp_vec_mul(const VfxWarpVec a, const f32 scalar) {
  return (VfxWarpVec){.x = a.x * scalar, .y = a.y * scalar};
}

static VfxWarpVec vfx_warp_vec_rand_in_box(Rng* rng, const f32 min, const f32 max) {
  return (VfxWarpVec){.x = rng_sample_range(rng, min, max), .y = rng_sample_range(rng, min, max)};
}

static bool vfx_warp_vec_eq(const VfxWarpVec a, const VfxWarpVec b, const f32 threshold) {
  return math_abs(a.x - b.x) <= threshold && math_abs(a.y - b.y) <= threshold;
}

static VfxWarpVec vfx_warp_apply(const VfxWarp* warp, const VfxWarpVec p) {
  const f32 w = 1.0f / (warp->columns[0].z * p.x + warp->columns[1].z * p.y + warp->columns[2].z);
  const f32 x = warp->columns[0].x * p.x + warp->columns[1].x * p.y + warp->columns[2].x;
  const f32 y = warp->columns[0].y * p.x + warp->columns[1].y * p.y + warp->columns[2].y;
  return (VfxWarpVec){.x = x * w, .y = y * w};
}

static VfxWarp vfx_warp_invert(const VfxWarp* w) {
  const f32 d0 = w->columns[1].y * w->columns[2].z - w->columns[2].y * w->columns[1].z;
  const f32 d1 = w->columns[2].x * w->columns[1].z - w->columns[1].x * w->columns[2].z;
  const f32 d2 = w->columns[1].x * w->columns[2].y - w->columns[2].x * w->columns[1].y;
  const f32 d  = w->columns[0].x * d0 + w->columns[0].y * d1 + w->columns[0].z * d2;
  diag_assert_msg(math_abs(d) > f32_epsilon, "Singular vfx warp matrix");
  const f32 dInv = 1.0f / d;
  return (VfxWarp){
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

static VfxWarp vfx_warp_ident() {
  return (VfxWarp){
      .columns = {
          {1, 0, 0},
          {0, 1, 0},
          {0, 0, 1},
      }};
}

static VfxWarp vfx_warp_to_points(const VfxWarpVec p[PARAM_ARRAY_SIZE(4)]) {
  const VfxWarpVec d = vfx_warp_vec_add(vfx_warp_vec_sub(p[0], p[1]), vfx_warp_vec_sub(p[2], p[3]));
  if (math_abs(d.x) < f32_epsilon && math_abs(d.y) < f32_epsilon) {
    // Affine transformation.
    const VfxWarpVec to1 = vfx_warp_vec_sub(p[1], p[0]);
    const VfxWarpVec to2 = vfx_warp_vec_sub(p[2], p[1]);
    return (VfxWarp){
        .columns = {
            {to1.x, to1.y, 0.0f},
            {to2.x, to2.y, 0.0f},
            {p[0].x, p[0].y, 1.0f},
        }};
  }
  const VfxWarpVec d1  = vfx_warp_vec_sub(p[1], p[2]);
  const VfxWarpVec d2  = vfx_warp_vec_sub(p[3], p[2]);
  const f32        den = d1.x * d2.y - d2.x * d1.y;
  diag_assert_msg(math_abs(den) > f32_epsilon, "Singular vfx warp matrix");
  const f32        denInv = 1.0f / den;
  const f32        u      = (d.x * d2.y - d.y * d2.x) * denInv;
  const f32        v      = (d.y * d1.x - d.x * d1.y) * denInv;
  const VfxWarpVec to1    = vfx_warp_vec_sub(p[1], p[0]);
  const VfxWarpVec to3    = vfx_warp_vec_sub(p[3], p[0]);
  return (VfxWarp){
      .columns = {
          {to1.x + u * p[1].x, to1.y + u * p[1].y, u},
          {to3.x + v * p[3].x, to3.y + v * p[3].y, v},
          {p[0].x, p[0].y, 1.0f},
      }};
}

static VfxWarp vfx_warp_from_points(const VfxWarpVec p[PARAM_ARRAY_SIZE(4)]) {
  const VfxWarp w = vfx_warp_to_points(p);
  return vfx_warp_invert(&w);
}

void eq_warp_vec_impl(
    CheckTestContext* ctx, const VfxWarpVec a, const VfxWarpVec b, const SourceLoc src) {

  if (UNLIKELY(!vfx_warp_vec_eq(a, b, test_warp_vec_eq_threshold))) {
    const String msg = fmt_write_scratch("{} == {}", vfx_warp_vec_fmt(a), vfx_warp_vec_fmt(b));
    check_report_error(ctx, msg, src);
  }
}

#define check_eq_warp_vec(_A_, _B_) eq_warp_vec_impl(_testCtx, (_A_), (_B_), source_location())

spec(warp) {

  Rng* testRng;

  setup() { testRng = rng_create_xorwow(g_alloc_heap, 1337); }

  it("returns the same points when applying a identity warp") {
    const VfxWarp w = vfx_warp_ident();
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&w, p);
      check_eq_warp_vec(p, pWarped);
    }
  }

  it("returns the same points when applying a unit points warp") {
    const VfxWarpVec toPoints[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };
    const VfxWarp w = vfx_warp_to_points(toPoints);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&w, p);
      check_eq_warp_vec(p, pWarped);
    }
  }

  it("returns offset points when applying an offset point warp") {
    const VfxWarpVec offset      = {.x = 1.337f, .y = 0.42f};
    const VfxWarpVec toPoints[4] = {
        vfx_warp_vec_add((VfxWarpVec){0.0f, 0.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){1.0f, 0.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){1.0f, 1.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){0.0f, 1.0f}, offset),
    };
    const VfxWarp w = vfx_warp_to_points(toPoints);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&w, p);
      check_eq_warp_vec(vfx_warp_vec_add(p, offset), pWarped);
    }
  }

  it("returns flipped points when applying an flipped point warp") {
    const VfxWarpVec toPoints[4] = {
        {0.0f, 0.0f},
        {-1.0f, 0.0f},
        {-1.0f, -1.0f},
        {0.0f, -1.0f},
    };
    const VfxWarp w = vfx_warp_to_points(toPoints);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&w, p);
      check_eq_warp_vec(vfx_warp_vec_mul(p, -1.0f), pWarped);
    }
  }

  it("returns scaled points when applying an scaled point warp") {
    const VfxWarpVec toPoints[4] = {
        {0.0f, 0.0f},
        {2.0f, 0.0f},
        {2.0f, 2.0f},
        {0.0f, 2.0f},
    };
    const VfxWarp w = vfx_warp_to_points(toPoints);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&w, p);
      check_eq_warp_vec(vfx_warp_vec_mul(p, 2.0f), pWarped);
    }
  }

  it("can invert a identity warp") {
    const VfxWarp w    = vfx_warp_ident();
    const VfxWarp wInv = vfx_warp_invert(&w);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&wInv, p);
      check_eq_warp_vec(p, pWarped);
    }
  }

  it("can invert an offset warp") {
    const VfxWarpVec offset      = {.x = 1.337f, .y = 0.42f};
    const VfxWarpVec toPoints[4] = {
        vfx_warp_vec_add((VfxWarpVec){0.0f, 0.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){1.0f, 0.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){1.0f, 1.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){0.0f, 1.0f}, offset),
    };
    const VfxWarp w    = vfx_warp_to_points(toPoints);
    const VfxWarp wInv = vfx_warp_invert(&w);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&wInv, p);
      check_eq_warp_vec(vfx_warp_vec_sub(p, offset), pWarped);
    }
  }

  it("can invert a scale warp") {
    const VfxWarpVec toPoints[4] = {
        {0.0f, 0.0f},
        {2.0f, 0.0f},
        {2.0f, 2.0f},
        {0.0f, 2.0f},
    };
    const VfxWarp w    = vfx_warp_to_points(toPoints);
    const VfxWarp wInv = vfx_warp_invert(&w);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&wInv, p);
      check_eq_warp_vec(vfx_warp_vec_mul(p, 0.5f), pWarped);
    }
  }

  it("can map to a trapezium") {
    const VfxWarpVec unitPoints[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };
    const VfxWarpVec trapeziumPoints[4] = {
        {-0.1f, 0.0f},
        {1.2f, 0.0f},
        {0.75f, 1.0f},
        {0.15f, 1.0f},
    };
    const VfxWarp w = vfx_warp_to_points(trapeziumPoints);
    for (u32 i = 0; i != array_elems(unitPoints); ++i) {
      const VfxWarpVec p       = unitPoints[i];
      const VfxWarpVec pWarped = vfx_warp_apply(&w, p);
      check_eq_warp_vec(trapeziumPoints[i], pWarped);
    }
  }

  it("can map from a trapezium") {
    const VfxWarpVec unitPoints[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };
    const VfxWarpVec trapeziumPoints[4] = {
        {-0.1f, 0.0f},
        {1.2f, 0.0f},
        {0.75f, 1.0f},
        {0.15f, 1.0f},
    };
    const VfxWarp w = vfx_warp_from_points(trapeziumPoints);
    for (u32 i = 0; i != array_elems(unitPoints); ++i) {
      const VfxWarpVec p       = trapeziumPoints[i];
      const VfxWarpVec pWarped = vfx_warp_apply(&w, p);
      check_eq_warp_vec(unitPoints[i], pWarped);
    }
  }

  teardown() { rng_destroy(testRng); }
}
