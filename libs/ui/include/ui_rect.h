#pragma once
#include "core_types.h"

/**
 * 2D Rectangle.
 */
typedef union {
  struct {
    f32 x, y;
    f32 width, height;
  };
  f32 data[4];
} UiRect;

/**
 * Construct a new rectangle.
 */
#define ui_rect(_X_, _Y_, _WIDTH_, _HEIGHT_) ((UiRect){(_X_), (_Y_), (_WIDTH_), (_HEIGHT_)})
