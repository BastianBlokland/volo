#pragma once
#include "core_alloc.h"

struct sAllocator {
  Mem (*alloc)(Allocator*, usize size, usize align);
  void (*free)(Allocator*, Mem);
  usize (*minSize)(Allocator*);
  usize (*maxSize)(Allocator*);
  void (*reset)(Allocator*);
};

Allocator* alloc_heap_init();
void       alloc_heap_teardown();

Allocator* alloc_page_init();

Allocator* alloc_scratch_init();
void       alloc_scratch_teardown();
