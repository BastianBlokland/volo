#include "core_bits.h"
#include "core_math.h"

#include "alloc_internal.h"

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

static void* stdlib_alloc(usize size, usize align) {
  if (!size) {
    return null;
  }
  align = math_max(align, alignof(AllocStdHeader));
  size  = bits_align(size, align);

  const usize padding   = bits_padding(sizeof(AllocStdHeader), align);
  const usize totalSize = padding + sizeof(AllocStdHeader) + size;

  Mem mem = alloc_alloc(g_allocHeap, totalSize, align);
  if (UNLIKELY(!mem_valid(mem))) {
    return null;
  }

  AllocStdHeader* hdr = bits_ptr_offset(mem.ptr, padding);
  hdr->size           = size;
  hdr->padding        = padding;

  return bits_ptr_offset(hdr, sizeof(AllocStdHeader));
}

static void stdlib_free(void* ptr) {
  if (!ptr) {
    return;
  }
  AllocStdHeader* hdr = bits_ptr_offset(ptr, -(iptr)sizeof(AllocStdHeader));

  const usize totalSize = hdr->padding + sizeof(AllocStdHeader) + hdr->size;
  const Mem   mem       = mem_create(bits_ptr_offset(hdr, -(iptr)hdr->padding), totalSize);

  const int errnoPrev = errno; // Preserve errno to match the GNU-C library behavior.

  alloc_free(g_allocHeap, mem);

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
