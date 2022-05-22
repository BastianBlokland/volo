#include "core_alloc.h"
#include "core_annotation.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "core_search.h"
#include "core_shuffle.h"
#include "core_sort.h"

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

DynArray dynarray_create_over(Mem memory, const u32 stride) {
  diag_assert(stride);
  DynArray array = {
      .stride = stride,
      .align  = 1,
      .data   = memory,
  };
  return array;
}

void dynarray_destroy(DynArray* array) {
  if (array->alloc && LIKELY(mem_valid(array->data))) {
    // Having a allocator pointer (and a valid allocation) means we should free the backing memory.
    alloc_free(array->alloc, array->data);
  }
}

usize dynarray_size(const DynArray* array) { return array->size; }

static void dynarray_resize_grow(DynArray* array, const usize size) {
  diag_assert_msg(array->alloc, "DynArray without an allocator ran out of memory");

  const Mem newMem =
      alloc_alloc(array->alloc, bits_nextpow2_64(size * array->stride), array->align);
  diag_assert_msg(mem_valid(newMem), "Allocation failed");

  if (LIKELY(mem_valid(array->data))) {
    mem_cpy(newMem, array->data);
    alloc_free(array->alloc, array->data);
  }
  array->data = newMem;
}

INLINE_HINT static void dynarray_resize_internal(DynArray* array, const usize size) {
  if (UNLIKELY(size * array->stride > array->data.size)) {
    dynarray_resize_grow(array, size);
  }
  array->size = size;
}

void dynarray_resize(DynArray* array, const usize size) { dynarray_resize_internal(array, size); }

void dynarray_clear(DynArray* array) { array->size = 0; }

Mem dynarray_at(const DynArray* array, const usize idx, const usize count) {
  diag_assert(idx + count <= array->size);
  const usize offset = array->stride * idx;
  const usize size   = array->stride * count;
  return mem_create(bits_ptr_offset(array->data.ptr, offset), size);
}

Mem dynarray_push(DynArray* array, const usize count) {
  dynarray_resize_internal(array, array->size + count);
  const usize offset = array->stride * (array->size - count);
  const usize size   = array->stride * count;
  return mem_create(bits_ptr_offset(array->data.ptr, offset), size);
}

void dynarray_pop(DynArray* array, usize count) {
  diag_assert(count <= array->size);
  dynarray_resize_internal(array, array->size - count);
}

void dynarray_remove(DynArray* array, const usize idx, const usize count) {
  diag_assert(array->size >= idx + count);

  const usize newSize       = array->size - count;
  const usize entriesToMove = newSize - idx;
  if (entriesToMove) {
    const Mem dst = dynarray_at(array, idx, entriesToMove);
    const Mem src = dynarray_at(array, idx + count, entriesToMove);
    mem_move(dst, src);
  }
  array->size = newSize;
}

void dynarray_remove_unordered(DynArray* array, const usize idx, const usize count) {
  diag_assert(array->size >= idx + count);

  const usize entriesToMove = math_min(count, array->size - (idx + count));
  if (entriesToMove) {
    const Mem dst = dynarray_at(array, idx, count);
    const Mem src = dynarray_at(array, array->size - entriesToMove, entriesToMove);
    mem_cpy(dst, src);
  }
  array->size -= count;
}

Mem dynarray_insert(DynArray* array, const usize idx, const usize count) {
  diag_assert(idx <= array->size);

  const usize entriesToMove = array->size - idx;
  dynarray_resize_internal(array, array->size + count);
  if (entriesToMove) {
    const Mem dst = dynarray_at(array, idx + count, entriesToMove);
    const Mem src = dynarray_at(array, idx, entriesToMove);
    mem_move(dst, src);
  }
  return dynarray_at(array, idx, count);
}

Mem dynarray_insert_sorted(
    DynArray* array, const usize count, CompareFunc compare, const void* target) {
  const Mem mem = dynarray_at(array, 0, array->size);
  void* ptr = search_binary_greater(mem_begin(mem), mem_end(mem), array->stride, compare, target);
  if (!ptr) {
    // No elements are greater; just insert at the end.
    return dynarray_push(array, count);
  }
  const usize idx = ((u8*)ptr - mem_begin(mem)) / array->stride;
  return dynarray_insert(array, idx, count);
}

void dynarray_sort(DynArray* array, CompareFunc compare) {
  const Mem mem = dynarray_at(array, 0, array->size);
  sort_quicksort(mem_begin(mem), mem_end(mem), array->stride, compare);
}

void* dynarray_search_linear(DynArray* array, CompareFunc compare, const void* target) {
  const Mem mem = dynarray_at(array, 0, array->size);
  return search_linear(mem_begin(mem), mem_end(mem), array->stride, compare, target);
}

void* dynarray_search_binary(DynArray* array, CompareFunc compare, const void* target) {
  const Mem mem = dynarray_at(array, 0, array->size);
  return search_binary(mem_begin(mem), mem_end(mem), array->stride, compare, target);
}

void* dynarray_find_or_insert_sorted(DynArray* array, CompareFunc compare, const void* target) {
  /**
   * Do a binary-search for the first entry that compares 'greater', which means the target has to
   * be the one before that. If its not that means the target is not in the array.
   */
  const Mem mem          = dynarray_at(array, 0, array->size);
  void*     begin        = mem_begin(mem);
  void*     end          = mem_end(mem);
  void*     greater      = search_binary_greater(begin, end, array->stride, compare, target);
  void*     greaterOrEnd = greater ? greater : end;

  // Check if the entry before the greater entry matches the given target.
  void* prev = bits_ptr_offset(greaterOrEnd, -(iptr)array->stride);
  if (prev >= begin && compare(prev, target) == 0) {
    return prev; // Existing entry found.
  }

  // Insert a new item at the 'greater' location (maintains sorting).
  const usize idx = ((u8*)greaterOrEnd - (u8*)begin) / array->stride;
  Mem         res = dynarray_insert(array, idx, 1);
  mem_set(res, 0); // Clear the new memory.
  return res.ptr;
}

void dynarray_shuffle(DynArray* array, Rng* rng) {
  const Mem mem = dynarray_at(array, 0, array->size);
  shuffle_fisheryates(rng, mem_begin(mem), mem_end(mem), array->stride);
}

void* dynarray_copy_as_new(const DynArray* array, Allocator* alloc) {
  if (!array->size) {
    return null;
  }
  const Mem arrayMem = dynarray_at(array, 0, array->size);
  const Mem newMem   = alloc_alloc(alloc, arrayMem.size, array->align);
  mem_cpy(newMem, arrayMem);
  return newMem.ptr;
}
