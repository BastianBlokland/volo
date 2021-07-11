#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_time.h"

struct sRng {
  u32 (*next)(Rng*);
  void (*destroy)(Rng*);
};

struct RngXorWow {
  Rng        api;
  u32        state[5];
  Allocator* alloc;
};

/**
 * Implementation of the 'splitmix' algorithm.
 * Source: https://en.wikipedia.org/wiki/Xorshift#xorwow
 */
static u64 rng_splitmix64(u64* state) {
  u64 result = *state += 0x9E3779B97f4A7C15;
  result     = (result ^ (result >> 30)) * 0xBF58476D1CE4E5B9;
  result     = (result ^ (result >> 27)) * 0x94D049BB133111EB;
  return result ^ (result >> 31);
}

static void rng_xorwow_init(struct RngXorWow* xorwow, u64 seed) {
  // Initialize the state for xorwow using the splitmix algorithm.
  const u64 val1   = rng_splitmix64(&seed);
  const u64 val2   = rng_splitmix64(&seed);
  xorwow->state[0] = (u32)val1;
  xorwow->state[1] = (u32)(val1 >> 32);
  xorwow->state[2] = (u32)val2;
  xorwow->state[3] = (u32)(val2 >> 32);
  xorwow->state[4] = 0;
}

static void rng_xorwow_destroy(Rng* rng) {
  struct RngXorWow* rngXorWow = (struct RngXorWow*)rng;
  alloc_free_t(rngXorWow->alloc, rngXorWow);
}

static u32 rng_xorwow_next(Rng* rng) {
  struct RngXorWow* rngXorWow = (struct RngXorWow*)rng;

  diag_assert(rngXorWow->state[0]);
  diag_assert(rngXorWow->state[1]);
  diag_assert(rngXorWow->state[2]);
  diag_assert(rngXorWow->state[3]);

  u32*      counter   = &rngXorWow->state[4];
  u32       t         = rngXorWow->state[3];
  const u32 s         = rngXorWow->state[0];
  rngXorWow->state[3] = rngXorWow->state[2];
  rngXorWow->state[2] = rngXorWow->state[1];
  rngXorWow->state[1] = s;

  t ^= t >> 2U;
  t ^= t << 1U;
  t ^= s ^ (s << 4U);
  rngXorWow->state[0] = t;

  *counter += 362437U;
  return t + *counter;
}

THREAD_LOCAL struct RngXorWow g_rng_xorwow = {.api = {.next = rng_xorwow_next}};
THREAD_LOCAL Rng* g_rng;

void rng_init_thread() {
  rng_xorwow_init(&g_rng_xorwow, time_real_clock());
  g_rng = (Rng*)&g_rng_xorwow;
}

f32 rng_sample_f32(Rng* rng) {
  diag_assert_msg(rng, "rng_next: Rng is not initialized");
  static const f32 toFloat = 1.0f / ((f32)u32_max + 1.0f); // +1 to never return 1.0.
  return rng->next(rng) * toFloat;
}

RngGaussPairF32 rng_sample_gauss_f32(Rng* rng) {
  f32 a, b;
  do {
    a = rng_sample_f32(rng);
    b = rng_sample_f32(rng);
    // Guard against a value very close to zero as we will feed it into std::log.
  } while (a <= 1e-8f);
  /**
   * BoxMuller transform.
   * Source: https://en.wikipedia.org/wiki/Box%E2%80%93Muller_transform
   */
  return (RngGaussPairF32){
      .a = math_sqrt_f32(-2.0f * math_log_f32(a)) * math_cos_f32(math_pi_f32 * 2.0f * b),
      .b = math_sqrt_f32(-2.0f * math_log_f32(a)) * math_sin_f32(math_pi_f32 * 2.0f * b),
  };
}

Rng* rng_create_xorwow(Allocator* alloc, u64 seed) {
  diag_assert_msg(seed, "rng_create_xorwow: 0 seed is invalid");
  struct RngXorWow* rng = alloc_alloc_t(alloc, struct RngXorWow);
  rng->api              = (Rng){
      .next    = rng_xorwow_next,
      .destroy = rng_xorwow_destroy,
  };
  rng->alloc = alloc;
  rng_xorwow_init(rng, seed);
  return (Rng*)rng;
}

void rng_destroy(Rng* rng) {
  diag_assert_msg(rng->destroy, "rng_destroy: Given Rng cannot be destroyed");
  rng->destroy(rng);
}
