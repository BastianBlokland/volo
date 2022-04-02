#pragma once
#include "core_alloc.h"
#include "core_diag.h"
#include "core_format.h"

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
    diag_crash();                                                                                  \
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

Allocator* alloc_heap_init();
void       alloc_heap_teardown();

Allocator* alloc_page_init();
u32        alloc_page_allocated_pages();
usize      alloc_page_allocated_size();
u64        alloc_page_counter(); // Incremented on every page allocation.

Allocator* alloc_persist_init();
void       alloc_persist_teardown();

Allocator* alloc_scratch_init();
void       alloc_scratch_teardown();

usize alloc_block_allocated_blocks(Allocator*);

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
