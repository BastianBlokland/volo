#include "core.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "core_search.h"
#include "core_shuffle.h"
#include "core_sort.h"

INLINE_HINT static Mem dynarray_at_internal(const DynArray* a, const usize idx, const usize count) {
  const usize offset = a->stride * idx;
  const usize size   = a->stride * count;
  return mem_create(bits_ptr_offset(a->data.ptr, offset), size);
}

DynArray
dynarray_create(Allocator* alloc, const u32 stride, const u16 align, const usize capacity) {
  diag_assert(stride);
  DynArray array = {
      .stride = stride,
      .align  = align,
      .alloc  = alloc,
  };
  if (capacity) {
    const usize capacityBytes = bits_nextpow2_64(capacity * stride);
    array.data                = alloc_alloc(alloc, capacityBytes, align);
    diag_assert_msg(mem_valid(array.data), "Allocation failed");
  }
  return array;
}

DynArray dynarray_create_over(const Mem memory, const u32 stride) {
  diag_assert(stride);
  DynArray array = {
      .stride = stride,
      .align  = 1,
      .data   = memory,
  };
  return array;
}

void dynarray_destroy(DynArray* a) {
  if (a->alloc && LIKELY(mem_valid(a->data))) {
    // Having a allocator pointer (and a valid allocation) means we should free the backing memory.
    alloc_free(a->alloc, a->data);
  }
}

usize dynarray_size(const DynArray* a) { return a->size; }

NO_INLINE_HINT static void dynarray_resize_grow(DynArray* a, const usize capacity) {
  diag_assert_msg(a->alloc, "DynArray without an allocator ran out of memory");

  const Mem newMem = alloc_alloc(a->alloc, bits_nextpow2_64(capacity * a->stride), a->align);
  diag_assert_msg(mem_valid(newMem), "Allocation failed");

  if (LIKELY(mem_valid(a->data))) {
    mem_cpy(newMem, a->data);
    alloc_free(a->alloc, a->data);
  }
  a->data = newMem;
}

INLINE_HINT static void dynarray_resize_internal(DynArray* a, const usize size) {
  if (UNLIKELY(size * a->stride > a->data.size)) {
    dynarray_resize_grow(a, size);
  }
  a->size = size;
}

void dynarray_resize(DynArray* a, const usize size) { dynarray_resize_internal(a, size); }

void dynarray_reserve(DynArray* a, const usize capacity) {
  if (UNLIKELY(capacity * a->stride > a->data.size)) {
    dynarray_resize_grow(a, capacity);
  }
}

void dynarray_clear(DynArray* a) { a->size = 0; }

Mem dynarray_at(const DynArray* a, const usize idx, const usize count) {
  diag_assert(idx + count <= a->size);
  return dynarray_at_internal(a, idx, count);
}

Mem dynarray_push(DynArray* a, const usize count) {
  dynarray_resize_internal(a, a->size + count);
  const usize offset = a->stride * (a->size - count);
  const usize size   = a->stride * count;
  return mem_create(bits_ptr_offset(a->data.ptr, offset), size);
}

void dynarray_pop(DynArray* a, const usize count) {
  diag_assert(count <= a->size);
  dynarray_resize_internal(a, a->size - count);
}

void dynarray_remove(DynArray* a, const usize idx, const usize count) {
  diag_assert(a->size >= idx + count);

  const usize newSize       = a->size - count;
  const usize entriesToMove = newSize - idx;
  if (entriesToMove) {
    const Mem dst = dynarray_at_internal(a, idx, entriesToMove);
    const Mem src = dynarray_at_internal(a, idx + count, entriesToMove);
    mem_move(dst, src);
  }
  a->size = newSize;
}

void dynarray_remove_unordered(DynArray* a, const usize idx, const usize count) {
  diag_assert(a->size >= idx + count);

  const usize entriesToMove = math_min(count, a->size - (idx + count));
  if (entriesToMove) {
    const Mem dst = dynarray_at_internal(a, idx, count);
    const Mem src = dynarray_at_internal(a, a->size - entriesToMove, entriesToMove);
    mem_cpy(dst, src);
  }
  a->size -= count;
}

Mem dynarray_insert(DynArray* a, const usize idx, const usize count) {
  diag_assert(idx <= a->size);

  const usize entriesToMove = a->size - idx;
  dynarray_resize_internal(a, a->size + count);
  if (entriesToMove) {
    const Mem dst = dynarray_at_internal(a, idx + count, entriesToMove);
    const Mem src = dynarray_at_internal(a, idx, entriesToMove);
    mem_move(dst, src);
  }
  return dynarray_at_internal(a, idx, count);
}

Mem dynarray_insert_sorted(
    DynArray* a, const usize count, CompareFunc compare, const void* target) {
  const Mem mem = dynarray_at_internal(a, 0, a->size);
  void*     ptr = search_binary_greater(mem_begin(mem), mem_end(mem), a->stride, compare, target);
  if (!ptr) {
    // No elements are greater; just insert at the end.
    return dynarray_push(a, count);
  }
  const usize idx = ((u8*)ptr - mem_begin(mem)) / a->stride;
  return dynarray_insert(a, idx, count);
}

void dynarray_sort(DynArray* a, CompareFunc compare) {
  const Mem mem = dynarray_at_internal(a, 0, a->size);
  sort_quicksort(mem_begin(mem), mem_end(mem), a->stride, compare);
}

void* dynarray_search_linear(DynArray* a, CompareFunc compare, const void* target) {
  const Mem mem = dynarray_at_internal(a, 0, a->size);
  return search_linear(mem_begin(mem), mem_end(mem), a->stride, compare, target);
}

void* dynarray_search_binary(DynArray* a, CompareFunc compare, const void* target) {
  const Mem mem = dynarray_at_internal(a, 0, a->size);
  return search_binary(mem_begin(mem), mem_end(mem), a->stride, compare, target);
}

void* dynarray_find_or_insert_sorted(DynArray* a, CompareFunc compare, const void* target) {
  /**
   * Do a binary-search for the first entry that compares 'greater', which means the target has to
   * be the one before that. If its not that means the target is not in the array.
   */
  const Mem mem = dynarray_at_internal(a, 0, a->size);
  if (!mem.size) {
    Mem res = dynarray_push(a, 1);
    mem_set(res, 0); // Clear the new memory.
    return res.ptr;
  }
  void* begin        = mem_begin(mem);
  void* end          = mem_end(mem);
  void* greater      = search_binary_greater(begin, end, a->stride, compare, target);
  void* greaterOrEnd = greater ? greater : end;

  // Check if the entry before the greater entry matches the given target.
  void* prev = bits_ptr_offset(greaterOrEnd, -(iptr)a->stride);
  if (prev >= begin && compare(prev, target) == 0) {
    return prev; // Existing entry found.
  }

  // Insert a new item at the 'greater' location (maintains sorting).
  const usize idx = ((u8*)greaterOrEnd - (u8*)begin) / a->stride;
  Mem         res = dynarray_insert(a, idx, 1);
  mem_set(res, 0); // Clear the new memory.
  return res.ptr;
}

void dynarray_shuffle(DynArray* a, Rng* rng) {
  const Mem mem = dynarray_at_internal(a, 0, a->size);
  shuffle_fisheryates(rng, mem_begin(mem), mem_end(mem), a->stride);
}

void* dynarray_copy_as_new(const DynArray* a, Allocator* alloc) {
  if (!a->size) {
    return null;
  }
  const Mem arrayMem = dynarray_at_internal(a, 0, a->size);
  const Mem newMem   = alloc_alloc(alloc, arrayMem.size, a->align);
  mem_cpy(newMem, arrayMem);
  return newMem.ptr;
}
