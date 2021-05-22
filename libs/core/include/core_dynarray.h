#pragma once
#include "core_alignof.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_memory.h"
#include "core_types.h"

/**
 * Owning array of items.
 * Dynamically allocates memory when more items get added.
 * Note: Any pointers / memory-views retreived over DynArray are invalidated on any mutating api.
 */
typedef struct {
  Mem        data;
  Allocator* alloc;
  usize      size;
  u16        stride;
} DynArray;

/**
 * Create a new dynamic array for items of type '_TYPE_'.
 * '_CAPACITY_' determines the size of the initial allocation, further allocations will be made
 * automatically when more memory is needed. '_CAPACITY_' of 0 is valid and won't allocate memory
 * until required.
 */
#define dynarray_create_t(_ALLOCATOR_, _TYPE_, _CAPACITY_)                                         \
  dynarray_create(_ALLOCATOR_, bits_align(sizeof(_TYPE_), alignof(_TYPE_)), _CAPACITY_)

/**
 * Retreive a pointer to an item in the array at index '_IDX_'.
 * Pre-condition: '_IDX_' < '_ARRAY_'.size
 * Pre-condition: sizeof(_TYPE_) <= '_ARRAY_'.stride
 */
#define dynarray_at_t(_ARRAY_, _IDX_, _TYPE_) mem_as_t(dynarray_at(_ARRAY_, _IDX_, 1), _TYPE_)

/**
 * Push memory for new item to the array. Returns a pointer to the new item.
 * Note: The memory for the new item is NOT initialized.
 * Pre-condition: sizeof(_TYPE_) <= '_ARRAY_'.stride
 */
#define dynarray_push_t(_ARRAY_, _TYPE_) mem_as_t(dynarray_push(_ARRAY_, 1), _TYPE_)

/**
 * Iterate over all items in the array.
 * Pre-condition: sizeof(_TYPE_) <= '_ARRAY_'.stride
 */
#define dynarray_for_t(_ARRAY_, _TYPE_, _VAR_, ...)                                                \
  {                                                                                                \
    DynArray* _VAR_##_array = (_ARRAY_);                                                           \
    for (usize _VAR_##_i = 0; _VAR_##_i != _VAR_##_array->size; ++_VAR_##_i) {                     \
      _TYPE_* _VAR_ = dynarray_at_t(_VAR_##_array, _VAR_##_i, _TYPE_);                             \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

/**
 * Create a new dynamic array, 'stride' determines the space each item occupies.
 * 'capacity' determines the size of the initial allocation, further allocations will be made
 * automatically when more memory is needed. 'capacity' of 0 is valid and won't allocate memory
 * until required.
 */
DynArray dynarray_create(Allocator*, u16 stride, usize capacity);

/**
 * Free resources held by the dynamic-array.
 */
void dynarray_destroy(DynArray*);

/**
 * Retrieve the current size (in elements) of the array.
 * Note: Identical to checking .size on the struct, but provided for consistency with other apis.
 */
usize dynarray_size(const DynArray*);

/**
 * Change the size of the dynamic-array, will allocate when size is bigger then the current
 * capacity.
 */
void dynarray_resize(DynArray*, usize size);

/**
 * Retrieve a memory view over the 'count' elements at index 'idx'
 * Pre-condition: idx + count <= array.size
 */
Mem dynarray_at(const DynArray*, usize idx, usize count);

/**
 * Push memory for new items to the array. Returns a memory-view over the new items.
 * Note: The memory for the new items is NOT initialized.
 */
Mem dynarray_push(DynArray*, usize count);

/**
 * Remove 'count' items from the end of the dynamic-array.
 * Pre-condition: count <= array.size
 */
void dynarray_pop(DynArray*, usize count);

/**
 * Remove 'count' items at index 'idx' from the dynamic-array.
 * Pre-condition: idx + count < array.size
 */
void dynarray_remove(DynArray*, usize idx, usize count);

/**
 * Insert 'count' items at index 'idx' in the dynamic-array. Returns a memory-view over the new
 * items.
 * Pre-condition: idx <= array.size
 */
Mem dynarray_insert(DynArray*, usize idx, usize count);
