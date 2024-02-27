#include "check_spec.h"
#include "core_alloc.h"
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

static VfxWarp vfx_warp_ident() {
  return (VfxWarp){
      .columns = {
          {1, 0, 0},
          {0, 1, 0},
          {0, 0, 1},
      }};
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
      const VfxWarpVec p       = vfx_warp_vec_rand_in_box(testRng, -100.0f, 100.0f);
      const VfxWarpVec pWarped = vfx_warp_apply(&w, p);
      check_eq_warp_vec(p, pWarped);
    }
  }

  teardown() { rng_destroy(testRng); }
}
