#pragma once
#include "data_registry.h"

/**
 * Deep-copy the original value into the given data memory.
 *
 * Pre-condition: original.size == clone.size.
 * Pre-condition: original memory contains an initialized value compatble with the given DataMeta.
 * Pre-condition: original memory does not contain any cycles.
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_clone(const DataReg*, Allocator* alloc, DataMeta, Mem original, Mem clone);

/**
 * Free the resources associated with the given value.
 *
 * Pre-condition: data resources where allocated from the given allocator.
 * Pre-condition: data memory contains an initialized value compatble with the given DataMeta.
 * Pre-condition: data memory does not contain any cycles.
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_destroy(const DataReg*, Allocator* alloc, DataMeta, Mem data);
