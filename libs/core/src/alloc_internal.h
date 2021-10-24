#pragma once
#include "core_alloc.h"

typedef enum {
  AllocMemType_Normal = 0,
  AllocMemType_Scratch,

  AllocMemType_Count,
} AllocMemType;

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

/**
 * Diagnostic apis that tag memory to detect uaf and buffer-overflows.
 */
void alloc_tag_free(Mem, AllocMemType);
void alloc_tag_guard(Mem, AllocMemType);
