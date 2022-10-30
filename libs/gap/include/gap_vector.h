#pragma once
#include "core_annotation.h"
#include "core_types.h"

/**
 * Tuple representing a 2d position or size.
 */
typedef union {
  struct {
    i32 x, y;
  };
  struct {
    i32 width, height;
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
 * Substract two vectors.
 * NOTE: Args are expanded twice, so care must be taken when providing complex expressions.
 */
#define gap_vector_sub(_A_, _B_) gap_vector((_A_).x - (_B_).x, (_A_).y - (_B_).y)

/**
 * Divide a vector by a scalar.
 * NOTE: _A_ is expanded twice, so care must be taken when providing complex expressions.
 */
#define gap_vector_div(_A_, _SCALAR_) gap_vector((_A_).x / (_SCALAR_), (_A_).y / (_SCALAR_))

/**
 * Create a formatting argument for a vector.
 * NOTE: _VAL_ is expanded twice, so care must be taken when providing complex expressions.
 */
#define gap_vector_fmt(_VAL_) fmt_list_lit(fmt_int((_VAL_).x), fmt_int((_VAL_).y))
