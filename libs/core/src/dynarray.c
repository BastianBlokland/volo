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
dynarray_create(Allocator* alloc, const u16 stride, const u16 align, const usize capacity) {
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

DynArray dynarray_create_over(Mem memory, u16 stride) {
  diag_assert(stride);
  DynArray array = {
      .stride = stride,
      .align  = 1,
      .data   = memory,
  };
  return array;
}

void dynarray_destroy(DynArray* array) {
  diag_assert(array);
  if (array->alloc && LIKELY(mem_valid(array->data))) {
    // Having a allocator pointer (and a valid allocation) means we should free the backing memory.
    alloc_free(array->alloc, array->data);
  }
}

INLINE_HINT usize dynarray_size(const DynArray* array) {
  diag_assert(array);
  return array->size;
}

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

INLINE_HINT void dynarray_resize(DynArray* array, const usize size) {
  diag_assert(array);
  if (size * array->stride > array->data.size) {
    dynarray_resize_grow(array, size);
  }
  array->size = size;
}

INLINE_HINT void dynarray_clear(DynArray* array) {
  diag_assert(array);
  array->size = 0;
}

INLINE_HINT Mem dynarray_at(const DynArray* array, const usize idx, const usize count) {
  diag_assert(array);
  diag_assert(idx + count <= array->size);
  return mem_slice(array->data, array->stride * idx, array->stride * count);
}

INLINE_HINT Mem dynarray_push(DynArray* array, const usize count) {
  dynarray_resize(array, array->size + count);
  return mem_slice(array->data, array->stride * (array->size - count), array->stride * count);
}

INLINE_HINT void dynarray_pop(DynArray* array, usize count) {
  diag_assert(array);
  diag_assert(count <= array->size);
  dynarray_resize(array, array->size - count);
}

void dynarray_remove(DynArray* array, const usize idx, const usize count) {
  diag_assert(array);
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
  diag_assert(array);
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
  diag_assert(array);
  diag_assert(idx <= array->size);

  const usize entriesToMove = array->size - idx;
  dynarray_resize(array, array->size + count);
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

void dynarray_shuffle(DynArray* array, Rng* rng) {
  const Mem mem = dynarray_at(array, 0, array->size);
  shuffle_fisheryates(rng, mem_begin(mem), mem_end(mem), array->stride);
}
