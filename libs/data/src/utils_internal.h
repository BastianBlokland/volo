#pragma once
#include "registry_internal.h"

/**
 * Get a memory view over a field in the given struct.
 */
Mem data_utils_field_mem(const DataDeclField*, Mem structMem);
