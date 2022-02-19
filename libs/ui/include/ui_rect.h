#pragma once
#include "ui_vector.h"

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
 * Construct a new rectangle.
 */
#define ui_rect(_POSITION_, _SIZE_) ((UiRect){(_POSITION_), (_SIZE_)})
