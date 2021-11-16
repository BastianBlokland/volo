#pragma once
#include "registry_internal.h"

/**
 * Get the size (in bytes) that a value of the given DataMeta occupies.
 */
usize data_utils_size(DataMeta);

/**
 * Get a memory view over a field in the given struct.
 */
Mem data_utils_field_mem(const DataDeclField*, Mem structMem);
