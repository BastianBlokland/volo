#pragma once
#include "core_dynstring.h"
#include "data_registry.h"

/**
 * Write a json-schema file for the given data-type.
 * Specification: https://json-schema.org/specification.html
 *
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_jsonschema_write(const DataReg*, DynString*, DataType rootType);
