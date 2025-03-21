#pragma once
#include "core.h"

typedef union uRvkSize {
  struct {
    u16 width, height;
  };
  u32 data;
} RvkSize;

#define rvk_size(_WIDTH_, _HEIGHT_) ((RvkSize){.width = (_WIDTH_), .height = (_HEIGHT_)})
#define rvk_size_equal(_A_, _B_) ((_A_).data == (_B_).data)
#define rvk_size_fmt(_VAL_) fmt_list_lit(fmt_int((_VAL_).width), fmt_int((_VAL_).height))
#define rvk_size_one ((RvkSize){1, 1})

RvkSize rvk_size_square(u16 size);
RvkSize rvk_size_scale(RvkSize size, f32 scale);
