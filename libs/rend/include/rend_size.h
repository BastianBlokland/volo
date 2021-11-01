#pragma once
#include "core_annotation.h"
#include "core_format.h"
#include "core_types.h"

/**
 * Tuple representing a 2d size.
 */
typedef union {
  struct {
    u32 width, height;
  };
  u64 data;
} RendSize;

ASSERT(sizeof(RendSize) == 8, "RendSize has to be 64 bits");

/**
 * Construct a new size.
 */
#define rend_size(_WIDTH_, _HEIGHT_) ((RendSize){.width = (_WIDTH_), .height = (_HEIGHT_)})

/**
 * Check if two sizes are equal.
 */
#define rend_size_equal(_A_, _B_) ((_A_).data == (_B_).data)

/**
 * Create a formatting argument for a size.
 * NOTE: _VAL_ is expanded twice, so care must be taken when providing complex expressions.
 */
#define rend_size_fmt(_VAL_) fmt_list_lit(fmt_int((_VAL_).width), fmt_int((_VAL_).height))
