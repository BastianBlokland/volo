#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "data/utils.h"

#include "registry.h"

static Mem container_push_heaparray(
    const DataReg* reg, Allocator* alloc, const DataMeta meta, const Mem data) {

  HeapArray* array = mem_as_t(data, HeapArray);

  const usize entrySize  = data_size(reg, meta.type);
  const usize entryAlign = data_align(reg, meta.type);

  Mem newArrayMem = alloc_alloc(alloc, entrySize * (array->count + 1), entryAlign);
  if (array->count) {
    diag_assert(array->values);

    const Mem oldArrayMem = mem_create(array->values, entrySize * array->count);
    mem_cpy(newArrayMem, oldArrayMem);
    alloc_free(g_allocHeap, oldArrayMem);
  } else {
    diag_assert(!array->values);
  }

  array->values = newArrayMem.ptr;
  array->count  = array->count + 1;

  return mem_slice(newArrayMem, entrySize * (array->count - 1), entrySize);
}

static Mem
container_push_dynarray(const DataReg* reg, Allocator* alloc, const DataMeta meta, const Mem data) {
  (void)alloc;
  (void)reg;
  (void)meta;

  DynArray* array = mem_as_t(data, DynArray);
  diag_assert(data_size(reg, meta.type) == array->stride);
  diag_assert(array->alloc == alloc);
  return dynarray_push(array, 1);
}

Mem data_container_push(const DataReg* reg, Allocator* alloc, const DataMeta meta, const Mem data) {
  switch (meta.container) {
  case DataContainer_None:
  case DataContainer_Pointer:
  case DataContainer_InlineArray:
    diag_assert_fail("Container does not support pushing new entries");
    return mem_empty;
  case DataContainer_HeapArray:
    return container_push_heaparray(reg, alloc, meta, data);
  case DataContainer_DynArray:
    return container_push_dynarray(reg, alloc, meta, data);
  }
  diag_crash_msg("Invalid container");
}

static void container_remove_heaparray(
    const DataReg* reg, Allocator* alloc, const DataMeta meta, const Mem data, const usize index) {

  HeapArray* array = mem_as_t(data, HeapArray);
  diag_assert(array->count);

  const usize entrySize  = data_size(reg, meta.type);
  const usize entryAlign = data_align(reg, meta.type);

  const Mem oldMem = mem_create(array->values, entrySize * array->count);

  const Mem toDestroyMem = mem_slice(oldMem, index * entrySize, entrySize);
  data_destroy(g_dataReg, g_allocHeap, data_meta_base(meta), toDestroyMem);

  const usize newCount = array->count - 1;
  if (!newCount) {
    alloc_free(alloc, oldMem);
    array->values = null;
    array->count  = 0;
    return;
  }

  const Mem newMem = alloc_alloc(alloc, entrySize * newCount, entryAlign);
  if (index) {
    // Copy the entries before the one to remove.
    mem_cpy(newMem, mem_slice(oldMem, 0, index * entrySize));
  }
  if (index < (array->count - 1)) {
    // Copy the entries after the one to remove.
    const usize dstOffset = index * entrySize;
    const usize srcOffset = (index + 1) * entrySize;
    mem_cpy(mem_consume(newMem, dstOffset), mem_slice(oldMem, srcOffset, oldMem.size - srcOffset));
  }

  alloc_free(alloc, oldMem);
  array->values = newMem.ptr;
  array->count  = newCount;
}

static void container_remove_dynarray(
    const DataReg* reg, Allocator* alloc, const DataMeta meta, const Mem data, const usize index) {

  DynArray* array = mem_as_t(data, DynArray);
  diag_assert(data_size(reg, meta.type) == array->stride);
  diag_assert(array->alloc == alloc);

  Mem entryMem = dynarray_at(array, index, 1);
  data_destroy(reg, alloc, data_meta_base(meta), entryMem);

  dynarray_remove(array, index, 1);
}

void data_container_remove(
    const DataReg* reg, Allocator* alloc, const DataMeta meta, const Mem data, const usize index) {
  switch (meta.container) {
  case DataContainer_None:
  case DataContainer_Pointer:
  case DataContainer_InlineArray:
    diag_assert_fail("Container does not support removing entries");
    return;
  case DataContainer_HeapArray:
    container_remove_heaparray(reg, alloc, meta, data, index);
    return;
  case DataContainer_DynArray:
    container_remove_dynarray(reg, alloc, meta, data, index);
    return;
  }
  diag_crash_msg("Invalid container");
}
