#include "core_array.h"
#include "core_diag.h"

#include "alloc_internal.h"
#include "init_internal.h"

#ifdef VOLO_ASAN
#include <sanitizer/asan_interface.h>
#endif

#define alloc_max_alloc_size (usize_mebibyte * 128)

Allocator*   g_alloc_heap;
Allocator*   g_alloc_page;
THREAD_LOCAL Allocator* g_alloc_scratch;

/**
 * Buffer to format crash messages without needing to allocate.
 * Which is needed as probably allocations are failing when we want to crash in the allocator.
 */
static DynString alloc_crash_msg_buffer() {
  static u8 buffer[128];
  return dynstring_create_over(array_mem(buffer));
}

#define alloc_crash_with_msg(_MSG_, ...)                                                           \
  do {                                                                                             \
    DynString buffer = alloc_crash_msg_buffer();                                                   \
    fmt_write(&buffer, "Crash: " _MSG_ "\n", __VA_ARGS__);                                         \
    diag_print_err_raw(dynstring_view(&buffer));                                                   \
    diag_crash();                                                                                  \
  } while (false)

static void alloc_verify_allocator(const Allocator* allocator) {
  if (UNLIKELY(allocator == null)) {
    alloc_crash_with_msg("Allocator is not initialized");
  }
}

void alloc_init() {
  g_alloc_page = alloc_page_init();
  g_alloc_heap = alloc_heap_init();
}

void alloc_teardown() {
  alloc_heap_teardown();

  const u32 leakedPages = alloc_page_allocated_pages();
  if (leakedPages) {
    alloc_crash_with_msg("alloc: {} pages leaked during app runtime", fmt_int(leakedPages));
  }
}

void alloc_init_thread() { g_alloc_scratch = alloc_scratch_init(); }

void alloc_teardown_thread() {
  alloc_scratch_teardown();
  g_alloc_scratch = null;
}

INLINE_HINT Mem alloc_alloc(Allocator* allocator, const usize size, const usize align) {
  alloc_verify_allocator(allocator);

  diag_assert_msg(size, "alloc_alloc: 0 byte allocations are not valid");
  diag_assert_msg(
      bits_ispow2(align), "alloc_alloc: Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      bits_aligned(size, align),
      "alloc_alloc: Size '{}' is not a multiple of the alignment '{}'",
      fmt_size(size),
      fmt_int(align));
  diag_assert_msg(
      size < alloc_max_alloc_size,
      "alloc_alloc: Size '{}' is bigger then the maximum of '{}'",
      fmt_size(size),
      fmt_size(alloc_max_alloc_size));

  return allocator->alloc(allocator, size, align);
}

INLINE_HINT void alloc_free(Allocator* allocator, Mem mem) {
  alloc_verify_allocator(allocator);
  allocator->free(allocator, mem);
}

Mem alloc_dup(Allocator* alloc, Mem mem, usize align) {
  Mem newMem = alloc_alloc(alloc, mem.size, align);
  mem_cpy(newMem, mem);
  return newMem;
}

INLINE_HINT usize alloc_min_size(Allocator* allocator) {
  alloc_verify_allocator(allocator);
  return allocator->minSize(allocator);
}

INLINE_HINT usize alloc_max_size(Allocator* allocator) {
  alloc_verify_allocator(allocator);
  return allocator->maxSize(allocator);
}

INLINE_HINT void alloc_reset(Allocator* allocator) {
  alloc_verify_allocator(allocator);

  diag_assert_msg(allocator->reset, "alloc_reset: Allocator does not support resetting");
  allocator->reset(allocator);
}

usize alloc_stats_total() { return alloc_page_allocated_size(); }

void alloc_tag_free(Mem mem, const AllocMemType type) {
  static const u8 tags[AllocMemType_Count] = {0xAA, 0xAB};
  mem_set(mem, tags[type]);
}

void alloc_tag_guard(Mem mem, const AllocMemType type) {
  static const u8 tags[AllocMemType_Count] = {0xBA, 0xBB};
  mem_set(mem, tags[type]);
}

void alloc_poison(Mem mem) {
  (void)mem;
#if defined(VOLO_ASAN)
  __asan_poison_memory_region(mem.ptr, mem.size);
#endif
}

void alloc_unpoison(Mem mem) {
  (void)mem;
#if defined(VOLO_ASAN)
  __asan_unpoison_memory_region(mem.ptr, mem.size);
#endif
}
