#pragma once
#include "core_alloc.h"

struct sAllocator {
  Mem (*alloc)(Allocator*, usize);
  void (*free)(Allocator*, Mem);
};
