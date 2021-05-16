#pragma once
#include "core_memory.h"

#define array_elems(array) (sizeof(array) / sizeof((array)[0]))

#define array_mem(array)                                                                           \
  ((Mem){                                                                                          \
      .ptr  = (void*)array,                                                                        \
      .size = sizeof(array),                                                                       \
  })
