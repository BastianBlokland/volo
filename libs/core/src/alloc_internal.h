#pragma once
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_symbol.h"

#include "diag_internal.h"

#ifndef VOLO_FAST
#define VOLO_MEMORY_TRACKING
#endif

#define alloc_max_alloc_size (usize_mebibyte * 256)

/**
 * Special crash-routine that does not allocate any memory.
 * Which is needed as probably allocations are failing when we want to crash in an allocator.
 */
#define alloc_crash_with_msg(_MSG_, ...)                                                           \
  do {                                                                                             \
    DynString buffer = dynstring_create_over(mem_stack(256));                                      \
    fmt_write(&buffer, "Crash: " _MSG_ "\n", __VA_ARGS__);                                         \
    diag_print_err_raw(dynstring_view(&buffer));                                                   \
    diag_pal_break();                                                                              \
    diag_pal_crash(); /* Unfortunately cannot include a stack, as symbol resolving allocates. */   \
  } while (false)

typedef enum {
  AllocMemType_Normal = 0,
  AllocMemType_Scratch,

  AllocMemType_Count,
} AllocMemType;

struct sAllocator {
  Mem (*alloc)(Allocator*, usize size, usize align);
  void (*free)(Allocator*, Mem);
  usize (*maxSize)(Allocator*);
  void (*reset)(Allocator*);
};

extern Allocator* g_allocPageCache;

Allocator* alloc_heap_init(void);
void       alloc_heap_leak_detect(void);
void       alloc_heap_teardown(void);
u64        alloc_heap_active(void);
u64        alloc_heap_counter(void); // Incremented on every heap allocation.

Allocator* alloc_page_init(void);
usize      alloc_page_size(void);
u32        alloc_page_allocated_pages(void);
usize      alloc_page_allocated_size(void);
u64        alloc_page_counter(void); // Incremented on every page allocation.

Allocator* alloc_pagecache_init(void);
void       alloc_pagecache_teardown(void);

Allocator* alloc_persist_init(void);
void       alloc_persist_teardown(void);
u64        alloc_persist_counter(void); // Incremented on every persist allocation.

Allocator* alloc_scratch_init(void);
void       alloc_scratch_teardown(void);

usize alloc_block_allocated_blocks(Allocator*);

/**
 * Diagnostic apis that write tag values to memory locations.
 * The tags are a low-tech solution for detecting UAF and buffer-overflows.
 */
void alloc_tag_new(Mem);
void alloc_tag_free(Mem, AllocMemType);
void alloc_tag_guard(Mem, AllocMemType);

/**
 * Diagnostic api for marking memory as poisoned.
 * Poisoned memory is not allowed to be read from / written to.
 */
void alloc_poison(Mem);
void alloc_unpoison(Mem);

/**
 * Allocation tracker.
 */
typedef struct sAllocTracker AllocTracker;

AllocTracker* alloc_tracker_create();
void          alloc_tracker_destroy(AllocTracker*);
void          alloc_tracker_add(AllocTracker*, Mem, SymbolStack);
void          alloc_tracker_remove(AllocTracker*, Mem);
usize         alloc_tracker_count(AllocTracker*);
usize         alloc_tracker_size(AllocTracker*);
void          alloc_tracker_dump(AllocTracker*, DynString* out);
void          alloc_tracker_dump_file(AllocTracker*, File* out);
