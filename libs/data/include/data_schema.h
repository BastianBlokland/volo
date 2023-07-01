#pragma once
#include "core_dynstring.h"
#include "data_registry.h"

/**
 * Write a json-schema file for the given data-type.
 * Specification: https://json-schema.org/specification.html
 *
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_jsonschema_write(const DataReg*, DynString*, DataMeta);

/**
 * Write a treeschema file for the given data-type.
 * The treeschema format is used by the 'https://www.bastian.tech/tree/' tree editor.
 * Format: https://github.com/BastianBlokland/typedtree-editor#example-of-the-scheme-format
 *
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_treeschema_write(const DataReg*, DynString*, DataType rootType);
