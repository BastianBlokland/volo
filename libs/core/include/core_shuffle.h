#pragma once
#include "core_rng.h"

/**
 * Shuffle elements according using the given RandomNumberGenerator
 * Pre-condition: stride <= 1024.
 */
void shuffle_fisheryates(Rng*, u8* begin, u8* end, u16 stride);
