#pragma once
#include "data_registry.h"

/**
 * Free the resources associated with the given value.
 *
 * Pre-condition: data resources where allocated from the given allocator.
 * Pre-condition: data memory contains an initialized value compatble with the given DataMeta.
 * Pre-condition: data memory does not contain any cycles.
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_free(Allocator* alloc, DataMeta, Mem data);
