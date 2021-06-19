#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_math.h"
#include "core_rng.h"

static void test_rng_sample_between_0_and_1() {
  const static usize iterations = 100;
  for (i32 i = 0; i != iterations; ++i) {
    const float rnd = rng_sample_float(g_rng);
    diag_assert(rnd >= 0.0f);
    diag_assert(rnd < 1.0f);
  }
}

static void test_rng_xorwow_fixed_seed_returns_consistent_samples() {
  Allocator* alloc = alloc_bump_create_stack(256);

  const static u64 seed = 42;
  Rng*             rng  = rng_create_xorwow(alloc, seed);
  float            rnd;

  rnd = rng_sample_float(rng);
  diag_assert(math_abs(rnd - 0.711535692215f) < 1e-8f);

  rnd = rng_sample_float(rng);
  diag_assert(math_abs(rnd - 0.465909004211f) < 1e-8f);

  rnd = rng_sample_float(rng);
  diag_assert(math_abs(rnd - 0.701208055019f) < 1e-8f);

  rnd = rng_sample_float(rng);
  diag_assert(math_abs(rnd - 0.908043026924f) < 1e-8f);

  rng_destroy(rng);
}

void test_rng() {
  test_rng_sample_between_0_and_1();
  test_rng_xorwow_fixed_seed_returns_consistent_samples();
}
