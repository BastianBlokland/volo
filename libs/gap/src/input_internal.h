#pragma once
#include "core_bitset.h"
#include "gap_input.h"
#include "gap_vector.h"

#define gap_key_mask_size bits_to_bytes(GapKey_Count)

typedef struct {
  u8 data[gap_key_mask_size];
} GapKeySet;

typedef struct {
  GapVector values[GapParam_Count];
} GapParamSet;

void   gap_keyset_clear(GapKeySet*);
BitSet gap_keyset_bits(GapKeySet*);
bool   gap_keyset_test(const GapKeySet*, GapKey);
void   gap_keyset_set(GapKeySet*, GapKey);
void   gap_keyset_set_all(GapKeySet*, const GapKeySet* other);
void   gap_keyset_unset(GapKeySet*, GapKey);
void   gap_keyset_unset_all(GapKeySet*, const GapKeySet* other);
