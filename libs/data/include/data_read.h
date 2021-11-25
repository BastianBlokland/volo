#pragma once
#include "data_registry.h"

typedef enum {
  DataReadError_None,
  DataReadError_Malformed,
  DataReadError_MismatchedType,
  DataReadError_InvalidEnumEntry,
  DataReadError_FieldNotFound,
  DataReadError_InvalidField,
  DataReadError_NumberOutOfBounds,
} DataReadError;

/**
 * Read result.
 * On a successful read: error == DataReadError_None.
 * On a failed read: 'error' contains an error code and 'errorMsg' contains a human readable string.
 * NOTE: errorMsg is allocated in scratch memory.
 */
typedef struct {
  DataReadError error;
  String        errorMsg;
} DataReadResult;

typedef String (*DataReader)(const DataReg*, String, Allocator*, DataMeta, Mem, DataReadResult*);

/**
 * Read a data value from a json string.
 * NOTE: Data is left uninitialized in case of an error (does not require cleanup by the caller).
 *
 * Returns the remaining input.
 * The result is written to the given data memory.
 *
 * Pre-condition: data memory is big enough to hold a value with the given DataMeta.
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 * Pre-condition: res != null.
 */
String data_read_json(const DataReg*, String, Allocator*, DataMeta, Mem data, DataReadResult*);
