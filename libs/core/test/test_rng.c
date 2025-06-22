#include "check_spec.h"
#include "core_alloc.h"
#include "core_rng.h"

spec(rng) {

  it("can sample floats with a uniform distribution") {
    static const usize g_iterations = 10000;
    static const u64   g_seed       = 42;

    Allocator* alloc = alloc_bump_create_stack(256);
    Rng*       rng   = rng_create_xorwow(alloc, g_seed);

    f32 sum = 0.0f;
    for (usize i = 0; i != g_iterations; ++i) {
      const f32 rnd = rng_sample_f32(rng);
      check(rnd >= 0.0f);
      check(rnd < 1.0f);
      sum += rnd;
    }
    const f32 avg = sum / g_iterations;

    // Uniform distribution should average out to 0.5.
    // Note: Adding more iterations would allow a tighter tolerance here.
    check_eq_float(avg, 0.5f, 1e-2f);

    rng_destroy(rng);
  }

  it("can sample floats with a gaussian distribution") {
    static const usize g_iterations = 10000;
    static const u64   g_seed       = 42;

    Allocator* alloc = alloc_bump_create_stack(256);
    Rng*       rng   = rng_create_xorwow(alloc, g_seed);

    f32 sum = 0.0f;
    for (usize i = 0; i != g_iterations; ++i) {
      const RngGaussPairF32 rnd = rng_sample_gauss_f32(rng);
      sum += rnd.a;
      sum += rnd.b;
    }
    const f32 avg = sum / g_iterations;

    // Gaussian distribution should average out to 0.
    // Note: Adding more iterations would allow a tighter tolerance here.
    check_eq_float(avg, 0.0f, 1e-2f);

    rng_destroy(rng);
  }

  it("can sample random values in a specific range") {
    static const usize g_iterations = 10 * 1000;
    static const u64   g_seed       = 42;

    Allocator* alloc = alloc_bump_create_stack(256);
    Rng*       rng   = rng_create_xorwow(alloc, g_seed);

    for (usize i = 0; i != g_iterations; ++i) {
      const i32 val = (i32)rng_sample_range(rng, -10, 20);
      check(val >= -10);
      check(val < 20);
    }
    for (usize i = 0; i != g_iterations; ++i) {
      const i32 val = (i32)rng_sample_range(rng, 0, 1);
      check(val == 0);
    }

    rng_destroy(rng);
  }

  it("returns consistent sample results using xorwow with a fixed seed") {
    Allocator* alloc = alloc_bump_create_stack(256);

    static const u64 g_seed = 42;
    Rng*             rng    = rng_create_xorwow(alloc, g_seed);
    f32              rnd;

    rnd = rng_sample_f32(rng);
    check_eq_float(rnd, 0.711535692215f, 1e-8f);

    rnd = rng_sample_f32(rng);
    check_eq_float(rnd, 0.465909004211f, 1e-8f);

    rnd = rng_sample_f32(rng);
    check_eq_float(rnd, 0.701208055019f, 1e-8f);

    rnd = rng_sample_f32(rng);
    check_eq_float(rnd, 0.908043026924f, 1e-8f);

    rng_destroy(rng);
  }
}
