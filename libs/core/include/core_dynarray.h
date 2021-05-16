#pragma once
#include "core_alignof.h"
#include "core_bits.h"
#include "core_memory.h"
#include "core_types.h"

typedef struct {
  Mem   data;
  usize size;
  u16   stride;
} DynArray;

#define dynarray_init_t(type, capacity)                                                            \
  dynarray_init(bits_align(sizeof(type), alignof(type)), capacity)
#define dynarray_at_t(array, idx, type) mem_as_t(dynarray_at(array, idx, 1), type)
#define dynarray_push_t(array, type) mem_as_t(dynarray_push(array, 1), type)

DynArray dynarray_init(u16 stride, usize capacity);
void     dynarray_free(DynArray*);
void     dynarray_resize(DynArray*, usize size);
Mem      dynarray_at(const DynArray*, usize idx, usize count);
Mem      dynarray_push(DynArray*, usize count);
void     dynarray_pop(DynArray*, usize count);
void     dynarray_remove(DynArray*, usize idx, usize count);
Mem      dynarray_insert(DynArray*, usize idx, usize count);
