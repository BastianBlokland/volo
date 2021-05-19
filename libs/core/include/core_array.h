#pragma once
#include "core_memory.h"

#define array_elems(_ARRAY_) (sizeof(_ARRAY_) / sizeof((_ARRAY_)[0]))

#define array_mem(_ARRAY_)                                                                         \
  ((Mem){                                                                                          \
      .ptr  = (void*)(_ARRAY_),                                                                    \
      .size = sizeof(_ARRAY_),                                                                     \
  })
