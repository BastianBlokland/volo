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

#define dynarray_init_t(_TYPE_, _CAPACITY_)                                                        \
  dynarray_init(bits_align(sizeof(_TYPE_), alignof(_TYPE_)), _CAPACITY_)
#define dynarray_at_t(_ARRAY_, _IDX_, _TYPE_) mem_as_t(dynarray_at(_ARRAY_, _IDX_, 1), _TYPE_)
#define dynarray_push_t(_ARRAY_, _TYPE_) mem_as_t(dynarray_push(_ARRAY_, 1), _TYPE_)

#define dynarray_for_t(_ARRAY_, _TYPE_, _VAR_, ...)                                                \
  {                                                                                                \
    DynArray* _VAR_##_array = (_ARRAY_);                                                           \
    for (usize _VAR_##_i = 0; _VAR_##_i != _VAR_##_array->size; ++_VAR_##_i) {                     \
      _TYPE_* _VAR_ = dynarray_at_t(_VAR_##_array, _VAR_##_i, _TYPE_);                             \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

DynArray dynarray_init(u16 stride, usize capacity);
void     dynarray_free(DynArray*);
void     dynarray_resize(DynArray*, usize size);
Mem      dynarray_at(const DynArray*, usize idx, usize count);
Mem      dynarray_push(DynArray*, usize count);
void     dynarray_pop(DynArray*, usize count);
void     dynarray_remove(DynArray*, usize idx, usize count);
Mem      dynarray_insert(DynArray*, usize idx, usize count);
