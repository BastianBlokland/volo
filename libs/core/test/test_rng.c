#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"

static void test_rng_sample_f32() {
  const static usize iterations = 10000;
  const static u64   seed       = 42;

  Allocator* alloc = alloc_bump_create_stack(256);
  Rng*       rng   = rng_create_xorwow(alloc, seed);

  float sum = 0.0f;
  for (i32 i = 0; i != iterations; ++i) {
    const f32 rnd = rng_sample_f32(g_rng);
    diag_assert(rnd >= 0.0f);
    diag_assert(rnd < 1.0f);
    sum += rnd;
  }
  const float avg = sum / iterations;

  // Uniform distribution should average out to 0.5.
  // Note: Adding more iterations would allow a tighter tolerance here.
  diag_assert(math_abs(avg - 0.5f) < 1e-3f);

  rng_destroy(rng);
}

static void test_rng_sample_gauss_f32() {
  const static usize iterations = 10000;
  const static u64   seed       = 42;

  Allocator* alloc = alloc_bump_create_stack(256);
  Rng*       rng   = rng_create_xorwow(alloc, seed);

  float sum = 0.0f;
  for (i32 i = 0; i != iterations; ++i) {
    const RngGaussPairF32 rnd = rng_sample_gauss_f32(rng);
    sum += rnd.a;
    sum += rnd.b;
  }
  const float avg = sum / iterations;

  // Gaussian distribution should average out to 0.
  // Note: Adding more iterations would allow a tighter tolerance here.
  diag_assert(math_abs(avg) < 1e-2f);

  rng_destroy(rng);
}

static void test_rng_sample_range() {
  const static usize iterations = 100;
  const static u64   seed       = 42;

  Allocator* alloc = alloc_bump_create_stack(256);
  Rng*       rng   = rng_create_xorwow(alloc, seed);

  for (i32 i = 0; i != iterations; ++i) {
    i32 val = rng_sample_range(g_rng, -10, 20);
    diag_assert(val >= -10);
    diag_assert(val < 20);
  }

  rng_destroy(rng);
}

static void test_rng_xorwow_fixed_seed_returns_consistent_samples() {
  Allocator* alloc = alloc_bump_create_stack(256);

  const static u64 seed = 42;
  Rng*             rng  = rng_create_xorwow(alloc, seed);
  f32              rnd;

  rnd = rng_sample_f32(rng);
  diag_assert(math_abs(rnd - 0.711535692215f) < 1e-8f);

  rnd = rng_sample_f32(rng);
  diag_assert(math_abs(rnd - 0.465909004211f) < 1e-8f);

  rnd = rng_sample_f32(rng);
  diag_assert(math_abs(rnd - 0.701208055019f) < 1e-8f);

  rnd = rng_sample_f32(rng);
  diag_assert(math_abs(rnd - 0.908043026924f) < 1e-8f);

  rng_destroy(rng);
}

void test_rng() {
  test_rng_sample_f32();
  test_rng_sample_gauss_f32();
  test_rng_sample_range();
  test_rng_xorwow_fixed_seed_returns_consistent_samples();
}
