#pragma once
#include "core_types.h"

typedef union {
  struct {
    u16 width, height;
  };
  u32 data;
} RvkSize;

#define rvk_size(_WIDTH_, _HEIGHT_) ((RvkSize){.width = (_WIDTH_), .height = (_HEIGHT_)})
#define rvk_size_equal(_A_, _B_) ((_A_).data == (_B_).data)
#define rvk_size_fmt(_VAL_) fmt_list_lit(fmt_int((_VAL_).width), fmt_int((_VAL_).height))
