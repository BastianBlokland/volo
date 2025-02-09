#include "core_bits.h"
#include "core_diag.h"

#include "alloc_internal.h"
#include "init_internal.h"

#if defined(VOLO_ASAN)
#include <sanitizer/asan_interface.h>
#endif

Allocator*              g_allocHeap;
Allocator*              g_allocPage;
Allocator*              g_allocPageCache;
Allocator*              g_allocPersist;
THREAD_LOCAL Allocator* g_allocScratch;

#if defined(VOLO_ASAN)
const char* __asan_default_options() {
  /**
   * Disable the address-sanitizer leak detection.
   * Reason is we get allot of hits within proprietary graphics drivers which we cannot change and
   * for our own allocations we have a custom leak detector.
   */
  return "detect_leaks=0";
}
#endif

static void alloc_verify_allocator(const Allocator* allocator) {
  if (UNLIKELY(allocator == null)) {
    alloc_crash_with_msg("Allocator is not initialized");
  }
}

void alloc_init(void) {
  g_allocPage      = alloc_page_init();
  g_allocPageCache = alloc_pagecache_init();
  g_allocHeap      = alloc_heap_init();
  g_allocPersist   = alloc_persist_init();
}

void alloc_leak_detect(void) { alloc_heap_leak_detect(); }

void alloc_teardown(void) {
  alloc_persist_teardown();
  g_allocPersist = null;

  alloc_heap_teardown();
  g_allocHeap = null;

  alloc_pagecache_teardown();
  g_allocPageCache = null;

  const u32 leakedPages = alloc_page_allocated_pages();
  if (leakedPages) {
    alloc_crash_with_msg("alloc: {} pages leaked during app runtime", fmt_int(leakedPages));
  }
}

void alloc_init_thread(void) { g_allocScratch = alloc_scratch_init(); }

void alloc_teardown_thread(void) {
  alloc_scratch_teardown();
  g_allocScratch = null;
}

Mem alloc_alloc(Allocator* allocator, const usize size, const usize align) {
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
      size <= alloc_max_alloc_size,
      "alloc_alloc: Size '{}' is bigger then the maximum of '{}'",
      fmt_size(size),
      fmt_size(alloc_max_alloc_size));

  const Mem res = allocator->alloc(allocator, size, align);
#ifndef VOLO_FAST
  if (mem_valid(res)) {
    alloc_tag_new(res);
  }
#endif
  return res;
}

void alloc_free(Allocator* allocator, const Mem mem) {
  alloc_verify_allocator(allocator);
  diag_assert_msg(mem.size, "alloc_free: 0 byte allocations are not valid");

  if (LIKELY(allocator->free)) {
    allocator->free(allocator, mem);
  }
}

void alloc_maybe_free(Allocator* allocator, const Mem mem) {
  if (mem_valid(mem) && mem.size) {
    alloc_free(allocator, mem);
  }
}

Mem alloc_dup(Allocator* allocator, const Mem mem, const usize align) {
  const Mem newMem = alloc_alloc(allocator, mem.size, align);
  if (UNLIKELY(!mem_valid(newMem))) {
    return newMem; // Allocation failed.
  }
  mem_cpy(newMem, mem);
  return newMem;
}

Mem alloc_maybe_dup(Allocator* allocator, const Mem mem, const usize align) {
  if (mem_valid(mem) && mem.size) {
    return alloc_dup(allocator, mem, align);
  }
  return mem;
}

usize alloc_max_size(Allocator* allocator) {
  alloc_verify_allocator(allocator);
  return allocator->maxSize(allocator);
}

void alloc_reset(Allocator* allocator) {
  alloc_verify_allocator(allocator);

  diag_assert_msg(allocator->reset, "alloc_reset: Allocator does not support resetting");
  allocator->reset(allocator);
}

AllocStats alloc_stats_query(void) {
  return (AllocStats){
      .pageCount      = alloc_page_allocated_pages(),
      .pageTotal      = alloc_page_allocated_size(),
      .pageCounter    = alloc_page_counter(),
      .heapActive     = alloc_heap_active(),
      .heapCounter    = alloc_heap_counter(),
      .persistCounter = alloc_persist_counter(),
  };
}

void alloc_tag_new(const Mem mem) {
  (void)mem;
#ifndef VOLO_FAST
  mem_set(mem, 0xCD);
#endif
}

void alloc_tag_free(const Mem mem, const AllocMemType type) {
  (void)mem;
  (void)type;
#ifndef VOLO_FAST
  static const u8 g_tags[AllocMemType_Count] = {0xAA, 0xAB};
  mem_set(mem, g_tags[type]);
#endif
}

void alloc_tag_guard(const Mem mem, const AllocMemType type) {
  (void)mem;
  (void)type;
#ifndef VOLO_FAST
  static const u8 g_tags[AllocMemType_Count] = {0xBA, 0xBB};
  mem_set(mem, g_tags[type]);
#endif
}

void alloc_poison(const Mem mem) {
  (void)mem;
#if defined(VOLO_ASAN)
  __asan_poison_memory_region(mem.ptr, mem.size);
#endif
}

void alloc_unpoison(const Mem mem) {
  (void)mem;
#if defined(VOLO_ASAN)
  __asan_unpoison_memory_region(mem.ptr, mem.size);
#endif
}
