#pragma once
#include "core.h"
#include "data.h"

typedef enum {
  DataJsonSchemaFlags_None    = 0,
  DataJsonSchemaFlags_Compact = 1 << 0,
} DataJsonSchemaFlags;

/**
 * Write a json-schema file for the given data-type.
 * Specification: https://json-schema.org/specification.html
 *
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 */
void data_jsonschema_write(const DataReg*, DynString*, DataMeta, DataJsonSchemaFlags);
