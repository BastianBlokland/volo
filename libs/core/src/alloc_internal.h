#pragma once
#include "core_alloc.h"

struct sAllocator {
  Mem (*alloc)(Allocator*, usize);
  void (*free)(Allocator*, Mem);
  usize (*minSize)(Allocator*);
};

Allocator* alloc_heap_init();
Allocator* alloc_page_init();
