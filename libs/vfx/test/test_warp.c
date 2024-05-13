#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_rng.h"
#include "vfx_warp.h"

#define test_vec_eq_threshold 1e-6f

static VfxWarpVec test_vec_rand_in_box(Rng* rng, const f32 min, const f32 max) {
  return (VfxWarpVec){.x = rng_sample_range(rng, min, max), .y = rng_sample_range(rng, min, max)};
}

static void test_eq_vec_impl(
    CheckTestContext* ctx, const VfxWarpVec a, const VfxWarpVec b, const SourceLoc src) {

  if (UNLIKELY(!vfx_warp_vec_eq(a, b, test_vec_eq_threshold))) {
    const String msg = fmt_write_scratch("{} == {}", vfx_warp_vec_fmt(a), vfx_warp_vec_fmt(b));
    check_report_error(ctx, msg, src);
  }
}

#define check_eq_vec(_A_, _B_) test_eq_vec_impl(_testCtx, (_A_), (_B_), source_location())

spec(warp) {

  Rng* testRng;

  setup() { testRng = rng_create_xorwow(g_allocHeap, 1337); }

  it("matrix returns the same points when applying a identity warp") {
    const VfxWarpMatrix w = vfx_warp_matrix_ident();
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = test_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&w, p);
      check_eq_vec(p, pWarped);
    }
  }

  it("matrix returns the same points when applying a unit points warp") {
    const VfxWarpVec toPoints[4] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    };
    const VfxWarpMatrix w = vfx_warp_matrix_to_points(toPoints);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = test_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&w, p);
      check_eq_vec(p, pWarped);
    }
  }

  it("matrix returns offset points when applying an offset point warp") {
    const VfxWarpVec offset      = {.x = 1.337f, .y = 0.42f};
    const VfxWarpVec toPoints[4] = {
        vfx_warp_vec_add((VfxWarpVec){0.0f, 0.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){1.0f, 0.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){1.0f, 1.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){0.0f, 1.0f}, offset),
    };
    const VfxWarpMatrix w = vfx_warp_matrix_to_points(toPoints);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = test_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&w, p);
      check_eq_vec(vfx_warp_vec_add(p, offset), pWarped);
    }
  }

  it("matrix returns flipped points when applying an flipped point warp") {
    const VfxWarpVec toPoints[4] = {
        {0.0f, 0.0f},
        {-1.0f, 0.0f},
        {-1.0f, -1.0f},
        {0.0f, -1.0f},
    };
    const VfxWarpMatrix w = vfx_warp_matrix_to_points(toPoints);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = test_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&w, p);
      check_eq_vec(vfx_warp_vec_mul(p, -1.0f), pWarped);
    }
  }

  it("matrix returns scaled points when applying an scaled point warp") {
    const VfxWarpVec toPoints[4] = {
        {0.0f, 0.0f},
        {2.0f, 0.0f},
        {2.0f, 2.0f},
        {0.0f, 2.0f},
    };
    const VfxWarpMatrix w = vfx_warp_matrix_to_points(toPoints);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = test_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&w, p);
      check_eq_vec(vfx_warp_vec_mul(p, 2.0f), pWarped);
    }
  }

  it("matrix can invert a identity warp") {
    const VfxWarpMatrix w    = vfx_warp_matrix_ident();
    const VfxWarpMatrix wInv = vfx_warp_matrix_invert(&w);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = test_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&wInv, p);
      check_eq_vec(p, pWarped);
    }
  }

  it("matrix can invert an offset warp") {
    const VfxWarpVec offset      = {.x = 1.337f, .y = 0.42f};
    const VfxWarpVec toPoints[4] = {
        vfx_warp_vec_add((VfxWarpVec){0.0f, 0.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){1.0f, 0.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){1.0f, 1.0f}, offset),
        vfx_warp_vec_add((VfxWarpVec){0.0f, 1.0f}, offset),
    };
    const VfxWarpMatrix w    = vfx_warp_matrix_to_points(toPoints);
    const VfxWarpMatrix wInv = vfx_warp_matrix_invert(&w);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = test_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&wInv, p);
      check_eq_vec(vfx_warp_vec_sub(p, offset), pWarped);
    }
  }

  it("matrix can invert a scale warp") {
    const VfxWarpVec toPoints[4] = {
        {0.0f, 0.0f},
        {2.0f, 0.0f},
        {2.0f, 2.0f},
        {0.0f, 2.0f},
    };
    const VfxWarpMatrix w    = vfx_warp_matrix_to_points(toPoints);
    const VfxWarpMatrix wInv = vfx_warp_matrix_invert(&w);
    for (u32 i = 0; i != 100; ++i) {
      const VfxWarpVec p       = test_vec_rand_in_box(testRng, -10.0f, 10.0f);
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&wInv, p);
      check_eq_vec(vfx_warp_vec_mul(p, 0.5f), pWarped);
    }
  }

  it("matrix can map to a trapezium") {
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
    const VfxWarpMatrix w = vfx_warp_matrix_to_points(trapeziumPoints);
    for (u32 i = 0; i != array_elems(unitPoints); ++i) {
      const VfxWarpVec p       = unitPoints[i];
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&w, p);
      check_eq_vec(trapeziumPoints[i], pWarped);
    }
  }

  it("matrix can map from a trapezium") {
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
    const VfxWarpMatrix w = vfx_warp_matrix_from_points(trapeziumPoints);
    for (u32 i = 0; i != array_elems(unitPoints); ++i) {
      const VfxWarpVec p       = trapeziumPoints[i];
      const VfxWarpVec pWarped = vfx_warp_matrix_apply(&w, p);
      check_eq_vec(unitPoints[i], pWarped);
    }
  }

  teardown() { rng_destroy(testRng); }
}
