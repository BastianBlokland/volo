#pragma once
#include "core_bitset.h"
#include "gap_input.h"

#define gap_key_mask_size bits_to_bytes(GapKey_Count)

typedef struct {
  u8 data[gap_key_mask_size];
} GapKeySet;

void gap_keyset_clear(GapKeySet*);
bool gap_keyset_test(const GapKeySet*, GapKey);
void gap_keyset_set(GapKeySet*, GapKey);
void gap_keyset_unset(GapKeySet*, GapKey);
