#pragma once
#include "core_annotation.h"
#include "core_types.h"

/**
 * Get the next value in the random sequence.
 * Returns a value between min (inclusive) and max (exclusive) with a uniform distribution.
 */
#define rng_sample_range(_RNG_, _MIN_, _MAX_)                                                      \
  ((_MIN_) + ((_MAX_) - (_MIN_)) * rng_sample_f32(_RNG_))

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Random Number Generator.
 */
typedef struct sRng Rng;

/**
 * Global (thread local) random number generator seeded with the real-time at thread creation.
 * Do not use this for anything security related.
 */
extern THREAD_LOCAL Rng* g_rng;

/**
 * Get the next value in the random sequence.
 * Returns a u32 with a uniform distribution.
 */
u32 rng_sample_u32(Rng*);

/**
 * Get the next value in the random sequence.
 * Returns a f32 between 0.0 (inclusive) and 1.0 (exclusive) with a uniform distribution.
 */
f32 rng_sample_f32(Rng*);

typedef struct {
  f32 a, b;
} RngGaussPairF32;

/**
 * Get the next two values in the random sequence.
 * Note: Returns two values the used Box–Muller transform yields two values, if only one is needed
 * then the other can be discarded.
 *
 * Returns two f32's between 0.0 (inclusive) and 1.0 (exclusive) with a gaussian (normal)
 * distribution.
 */
RngGaussPairF32 rng_sample_gauss_f32(Rng*);

/**
 * Rng implementation using the xorwow algorithm.
 *
 * Uses a shift-register strategy, usefull for usecases where distribution and repetition period
 * are not critically important. Do not use this for anything security related.
 * Should be cleaned up using 'rng_destroy'.
 *
 * Pre-condition: seed != 0
 */
Rng* rng_create_xorwow(Allocator*, u64 seed);

/**
 * Destroy a previous created rng object.
 */
void rng_destroy(Rng*);
