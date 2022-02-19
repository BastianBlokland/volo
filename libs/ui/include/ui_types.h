#pragma once
#include "core_annotation.h"
#include "core_format.h"
#include "core_types.h"

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
 * 2D Rectangle.
 */
typedef union {
  struct {
    UiVector position, size;
  };
  f32 data[4];
} UiRect;

ASSERT(sizeof(UiRect) == 16, "UiRect has to be 128 bits");

/**
 * Construct a new vector.
 */
#define ui_vector(_X_, _Y_) ((UiVector){(_X_), (_Y_)})

/**
 * Construct a new rectangle.
 */
#define ui_rect(_POSITION_, _SIZE_) ((UiRect){(_POSITION_), (_SIZE_)})

/**
 * Create a formatting argument for a vector.
 * NOTE: _VAL_ is expanded twice, so care must be taken when providing complex expressions.
 */
#define ui_vector_fmt(_VAL_) fmt_list_lit(fmt_float((_VAL_).x), fmt_float((_VAL_).y))
