#pragma once
#include "core_annotation.h"
#include "core_format.h"

/**
 * 2D Vector.
 */
typedef union {
  struct {
    f32 x, y;
  };
  struct {
    f32 width, height;
  };
  f32 comps[2];
} UiVector;

ASSERT(sizeof(UiVector) == 8, "UiVector has to be 64 bits");

/**
 * Construct a new vector.
 */
#define ui_vector(_X_, _Y_) ((UiVector){(_X_), (_Y_)})

/**
 * Create a formatting argument for a vector.
 * NOTE: _VAL_ is expanded twice, so care must be taken when providing complex expressions.
 */
#define ui_vector_fmt(_VAL_) fmt_list_lit(fmt_float((_VAL_).x), fmt_float((_VAL_).y))
