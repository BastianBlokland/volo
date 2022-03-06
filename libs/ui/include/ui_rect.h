#pragma once
#include "ui_vector.h"

/**
 * 2D Rectangle.
 */
typedef union {
  struct {
    UiVector pos, size;
  };
  f32 data[4];
  struct {
    f32 x, y, width, height;
  };
} UiRect;

ASSERT(sizeof(UiRect) == 16, "UiRect has to be 128 bits");

/**
 * Construct a new rectangle.
 */
#define ui_rect(_POS_, _SIZE_) ((UiRect){(_POS_), (_SIZE_)})
