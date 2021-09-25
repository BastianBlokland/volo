#pragma once
#include "core_alignof.h"
#include "core_compare.h"
#include "core_memory.h"
#include "core_types.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'core_rng.h'.
typedef struct sRng Rng;

/**
 * Owning array of items.
 * Dynamically allocates memory when more items get added.
 * NOTE: Any pointers / memory-views retrieved over DynArray are invalidated on any mutating api.
 */
typedef struct {
  Mem        data;
  Allocator* alloc;
  usize      size;
  u16        stride;
  u16        align;
} DynArray;

/**
 * Create a new dynamic array for items of type '_TYPE_'.
 * '_CAPACITY_' determines the size of the initial allocation, further allocations will be made
 * automatically when more memory is needed. '_CAPACITY_' of 0 is valid and won't allocate memory
 * until required.
 */
#define dynarray_create_t(_ALLOCATOR_, _TYPE_, _CAPACITY_)                                         \
  dynarray_create((_ALLOCATOR_), (u16)sizeof(_TYPE_), (u16)alignof(_TYPE_), _CAPACITY_)

/**
 * Create a new dynamic array for items of type '_TYPE_' over the given memory.
 * Will not allocate any memory, pushing more entries then (mem.size / stride) is not supported.
 */
#define dynarray_create_over_t(_MEM_, _TYPE_) dynarray_create_over((_MEM_), (u16)sizeof(_TYPE_))

/**
 * Retreive a pointer to an item in the array at index '_IDX_'.
 * Pre-condition: '_IDX_' < '_ARRAY_'.size
 * Pre-condition: sizeof(_TYPE_) <= '_ARRAY_'.stride
 */
#define dynarray_at_t(_ARRAY_, _IDX_, _TYPE_) mem_as_t(dynarray_at(_ARRAY_, _IDX_, 1), _TYPE_)

/**
 * Push memory for new item to the array. Returns a pointer to the new item.
 * NOTE: The memory for the new item is NOT initialized.
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
 * Create a new dynamic array. 'stride' determines the space each item occupies and 'align'
 * specifies the required alignment for the memory allocation. 'capacity' determines the size of the
 * initial allocation, further allocations will be made automatically when more memory is needed.
 * 'capacity' of 0 is valid and won't allocate memory until required.
 */
DynArray dynarray_create(Allocator*, u16 stride, u16 align, usize capacity);

/**
 * Create a new dynamic array over the given memory, 'stride' determines the space each item
 * occupies.
 * Will not allocate any memory, pushing more entries then (mem.size / stride) is not supported.
 */
DynArray dynarray_create_over(Mem, u16 stride);

/**
 * Free resources held by the dynamic-array.
 */
void dynarray_destroy(DynArray*);

/**
 * Retrieve the current size (in elements) of the array.
 * NOTE: Identical to checking .size on the struct, but provided for consistency with other apis.
 */
usize dynarray_size(const DynArray*);

/**
 * Change the size of the dynamic-array, will allocate when size is bigger then the current
 * capacity.
 */
void dynarray_resize(DynArray*, usize size);

/**
 * Resizes the dynamic-array to be 0 length.
 */
void dynarray_clear(DynArray*);

/**
 * Retrieve a memory view over the 'count' elements at index 'idx'
 * Pre-condition: idx + count <= array.size
 */
Mem dynarray_at(const DynArray*, usize idx, usize count);

/**
 * Push memory for new items to the array. Returns a memory-view over the new items.
 * NOTE: The memory for the new items is NOT initialized.
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
 * Remove 'count' items at index 'idx' from the dynamic-array.
 * Elements from the end of the array will be moved into the created hole.
 * Pre-condition: idx + count < array.size
 */
void dynarray_remove_unordered(DynArray*, usize idx, usize count);

/**
 * Insert 'count' items at index 'idx' in the dynamic-array. Returns a memory-view over the new
 * items.
 * Pre-condition: idx <= array.size
 */
Mem dynarray_insert(DynArray*, usize idx, usize count);

/**
 * Insert 'count' items into the dynamic-array at an index that would maintain sorting with target.
 * Returns a memory-view over the new items.
 * Pre-condition: array is sorted.
 */
Mem dynarray_insert_sorted(DynArray*, usize count, CompareFunc, const void* target);

/**
 * Sort the array according to the given compare function.
 */
void dynarray_sort(DynArray*, CompareFunc);

/**
 * Search the array for an element matching the given target using a linear scan.
 */
void* dynarray_search_linear(DynArray*, CompareFunc, const void* target);

/**
 * Search the array for an element matching the given target using a binary scan.
 * Pre-condition: array is sorted.
 */
void* dynarray_search_binary(DynArray*, CompareFunc, const void* target);

/**
 * Shuffle the array using the given RandomNumberGenerator.
 */
void dynarray_shuffle(DynArray*, Rng*);
