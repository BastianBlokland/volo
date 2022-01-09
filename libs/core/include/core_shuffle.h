#pragma once
#include "core_rng.h"

/**
 * Shuffle elements according using the given RandomNumberGenerator
 * Pre-condition: stride <= 1024.
 */
#define shuffle_fisheryates_t(_RNG_, _BEGIN_, _END_, _TYPE_)                                       \
  shuffle_fisheryates((_RNG_), (u8*)(_BEGIN_), (u8*)(_END_), sizeof(_TYPE_))

/**
 * Shuffle elements according using the given RandomNumberGenerator
 * Pre-condition: stride <= 1024.
 */
void shuffle_fisheryates(Rng*, u8* begin, u8* end, u16 stride);
