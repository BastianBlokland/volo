#include "core_annotation.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"

DynArray dynarray_create(Allocator* alloc, const u16 stride, const usize capacity) {
  diag_assert(stride);
  DynArray array = {
      .stride = stride,
      .alloc  = alloc,
  };
  if (capacity) {
    const usize capacityBytes = bits_nextpow2(capacity * stride);
    array.data                = alloc_alloc(alloc, capacityBytes);
    diag_assert_msg(mem_valid(array.data), "Allocation failed");
  }
  return array;
}

void dynarray_destroy(DynArray* array) {
  diag_assert(array);
  if (likely(mem_valid(array->data))) {
    alloc_free(array->alloc, array->data);
  }
}

usize dynarray_size(const DynArray* array) {
  diag_assert(array);
  return array->size;
}

void dynarray_resize(DynArray* array, const usize size) {
  diag_assert(array);
  if (size * array->stride > array->data.size) {
    const Mem newMem = alloc_alloc(array->alloc, bits_nextpow2(size * array->stride));
    diag_assert_msg(mem_valid(newMem), "Allocation failed");

    if (likely(mem_valid(array->data))) {
      mem_cpy(newMem, array->data);
      alloc_free(array->alloc, array->data);
    }
    array->data = newMem;
  }
  array->size = size;
}

Mem dynarray_at(const DynArray* array, const usize idx, const usize count) {
  diag_assert(array);
  diag_assert(idx + count <= array->size);
  return mem_slice(array->data, array->stride * idx, array->stride * count);
}

Mem dynarray_push(DynArray* array, const usize count) {
  dynarray_resize(array, array->size + count);
  return mem_slice(array->data, array->stride * (array->size - count), array->stride * count);
}

void dynarray_pop(DynArray* array, usize count) {
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
