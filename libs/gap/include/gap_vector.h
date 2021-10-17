#pragma once
#include "core_annotation.h"
#include "core_format.h"
#include "core_types.h"

/**
 * Tuple representing a 2d position or size.
 */
typedef union {
  struct {
    u32 x, y;
  };
  struct {
    u32 width, height;
  };
  u64 data;
} GapVector;

ASSERT(sizeof(GapVector) == 8, "GapVector has to be 64 bits");

/**
 * Construct a new vector.
 */
#define gap_vector(_X_, _Y_) ((GapVector){.x = (_X_), .y = (_Y_)})

/**
 * Check if two vectors are equal.
 */
#define gap_vector_equal(_A_, _B_) ((_A_).data == (_B_).data)

/**
 * Create a formatting argument for a vector.
 * NOTE: _VAL_ is expanded twice, so care must be taken when providing complex expressions.
 */
#define gap_vector_fmt(_VAL_) fmt_list_lit(fmt_int((_VAL_).x), fmt_int((_VAL_).y))
