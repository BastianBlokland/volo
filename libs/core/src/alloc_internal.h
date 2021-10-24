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
u32        alloc_page_allocated_pages();
usize      alloc_page_allocated_size();

Allocator* alloc_scratch_init();
void       alloc_scratch_teardown();

/**
 * Diagnostic apis that write tag values to memory locations.
 * The tags are a low-tech solution for detecting UAF and buffer-overflows.
 */
void alloc_tag_free(Mem, AllocMemType);
void alloc_tag_guard(Mem, AllocMemType);

/**
 * Diagnostic api for marking memory as poisonned.
 * Poisoned memory is not allowed to be read from / written to.
 */
void alloc_poison(Mem);
void alloc_unpoison(Mem);
