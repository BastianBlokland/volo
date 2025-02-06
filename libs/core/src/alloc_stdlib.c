#include "core_bits.h"
#include "core_math.h"

#include "alloc_internal.h"

#ifdef VOLO_WIN32
/**
 * DISABLED: Overriding malloc on Win32 can't be done (as far as i know) directly from the
 * executable as dynamic libraries won't link to symbols from the executable.
 */
#define alloc_std_malloc_override 0
#else
/**
 * DISABLED: Our memory allocators do leak detection at shutdown, however allot of third party
 * dependencies don't free all their allocations. To avoid allot of false positives we need to
 * support suppressing leak detection for external allocations.
 */
#define alloc_std_malloc_override 0
#endif

#if alloc_std_malloc_override

#include <errno.h>
#include <stdlib.h>

/**
 * Implement the standard library malloc to use our heap-allocator.
 * This allows us to use our memory tracking for allocations from external libraries.
 *
 * Our allocators require the caller to track allocation sizes, to support this for the standard
 * malloc api we add a header to the beginning of the allocations.
 *
 * Allocation memory layout:
 * - [PADDING] (padding to satisfy the requested alignment)
 * - AllocStdHeader (16 bytes)
 * - [PAYLOAD]
 */

#define alloc_std_default_align 16

typedef struct {
  usize size, padding;
} AllocStdHeader;

NO_INLINE_HINT MAYBE_UNUSED static void stdlib_verify_size(const usize size, const usize align) {
  diag_assert_msg(
      bits_ispow2(align), "alloc_stdlib: Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      size <= alloc_max_alloc_size,
      "alloc_stdlib: Size '{}' is bigger then the maximum of '{}'",
      fmt_size(size),
      fmt_size(alloc_max_alloc_size));
}

INLINE_HINT static void* stdlib_alloc(usize size, usize align) {
  if (!size) {
    return null;
  }
#ifndef VOLO_FAST
  stdlib_verify_size(size, align);
#endif

  align = math_max(align, alignof(AllocStdHeader));
  size  = bits_align(size, align);

  const usize padding   = bits_padding(sizeof(AllocStdHeader), align);
  const usize totalSize = padding + sizeof(AllocStdHeader) + size;

  const Mem mem = g_allocHeap->alloc(g_allocHeap, totalSize, align);
  if (UNLIKELY(!mem_valid(mem))) {
    return null;
  }

  AllocStdHeader* hdr = bits_ptr_offset(mem.ptr, padding);
  hdr->size           = size;
  hdr->padding        = padding;

  void* res = bits_ptr_offset(hdr, sizeof(AllocStdHeader));

#ifndef VOLO_FAST
  alloc_tag_new(mem_create(res, size));
#endif

  return res;
}

INLINE_HINT static void stdlib_free(void* ptr) {
  if (!ptr) {
    return;
  }
  AllocStdHeader* hdr = bits_ptr_offset(ptr, -(iptr)sizeof(AllocStdHeader));

  const usize totalSize = hdr->padding + sizeof(AllocStdHeader) + hdr->size;
  const Mem   mem       = mem_create(bits_ptr_offset(hdr, -(iptr)hdr->padding), totalSize);

  const int errnoPrev = errno; // Preserve errno to match the GNU-C library behavior.

  g_allocHeap->free(g_allocHeap, mem);

  errno = errnoPrev;
}

static Mem stdlib_payload(void* ptr) {
  if (!ptr) {
    return mem_empty;
  }
  AllocStdHeader* hdr = bits_ptr_offset(ptr, -(iptr)sizeof(AllocStdHeader));
  return mem_create(ptr, hdr->size);
}

void* SYS_DECL malloc(const usize size) { return stdlib_alloc(size, alloc_std_default_align); }

void* SYS_DECL calloc(const usize num, const usize size) {
  const usize sizeTotal = num * size;
  void*       res       = stdlib_alloc(sizeTotal, alloc_std_default_align);
  if (LIKELY(res)) {
    mem_set(mem_create(res, sizeTotal), 0);
  }
  return res;
}

void SYS_DECL free(void* ptr) { stdlib_free(ptr); }

void SYS_DECL cfree(void* ptr) { stdlib_free(ptr); }

void* SYS_DECL realloc(void* ptr, const usize newSize) {
  void* newPtr = stdlib_alloc(newSize, alloc_std_default_align);
  if (UNLIKELY(!newPtr && !ptr)) {
    return null;
  }

  if (ptr) {
    if (newPtr) {
      const Mem   orgPayload  = stdlib_payload(ptr);
      const usize bytesToCopy = math_min(orgPayload.size, newSize);
      mem_cpy(mem_create(newPtr, bytesToCopy), mem_create(orgPayload.ptr, bytesToCopy));
    }
    stdlib_free(ptr);
  }

  return newPtr;
}

int SYS_DECL posix_memalign(void** outPtr, const usize align, const usize size) {
  if (!bits_aligned(align, sizeof(usize)) || !bits_ispow2(align)) {
    return EINVAL;
  }
  void* res = stdlib_alloc(size, align);
  if (res) {
    *outPtr = res;
    return 0;
  }
  return ENOMEM;
}

void* SYS_DECL aligned_alloc(const usize align, const usize size) {
  return stdlib_alloc(size, align);
}

void* SYS_DECL valloc(const usize size) { return stdlib_alloc(size, alloc_page_size()); }

void* SYS_DECL memalign(const usize align, const usize size) { return stdlib_alloc(size, align); }

void* SYS_DECL pvalloc(const usize size) { return stdlib_alloc(size, alloc_page_size()); }

usize SYS_DECL malloc_usable_size(void* ptr) { return stdlib_payload(ptr).size; }

void SYS_DECL free_sized(void* ptr, const usize size) {
  (void)size;
  stdlib_free(ptr);
}

void SYS_DECL free_aligned_sized(void* ptr, const usize align, const usize size) {
  (void)align;
  (void)size;
  stdlib_free(ptr);
}

#endif
