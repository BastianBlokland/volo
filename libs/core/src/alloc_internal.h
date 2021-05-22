#pragma once
#include "core_alloc.h"

struct sAllocator {
  Mem (*alloc)(Allocator*, usize);
  void (*free)(Allocator*, Mem);
  usize (*min_size)(Allocator*);
};

Allocator* alloc_init_heap();
Allocator* alloc_init_page();
