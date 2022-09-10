#pragma once
#include "core_dynstring.h"
#include "data_registry.h"

/**
 * Write a treescheme file for the given data-type.
 * The treescheme format is used by the 'https://www.bastian.tech/tree/' tree editor.
 * Format: https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 *
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_treescheme_write(const DataReg*, DynString*, DataType rootType);
