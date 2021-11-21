#pragma once
#include "core_dynstring.h"
#include "data_registry.h"

typedef String (*DataWriter)(DynString*, DataMeta, Mem data);

/**
 * Write a data value as a json string.
 *
 * Pre-condition: data memory contains an initialized value compatble with the given DataMeta.
 * Pre-condition: data memory does not contain any cycles.
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_write_json(DynString*, DataMeta, Mem data);
