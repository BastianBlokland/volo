#pragma once
#include "core_bits.h"
#include "gap_input.h"

typedef struct {
  u8 data[bits_to_bytes(GapKey_Count)];
} GapKeySet;

void gap_keyset_clear(GapKeySet*);
bool gap_keyset_test(const GapKeySet*, GapKey);
void gap_keyset_set(GapKeySet*, GapKey);
void gap_keyset_unset(GapKeySet*, GapKey);
