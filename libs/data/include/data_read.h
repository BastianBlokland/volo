#pragma once
#include "data_registry.h"

typedef enum {
  DataReadError_None,
  DataReadError_Corrupted,
  DataReadError_Malformed,
  DataReadError_Incompatible,
  DataReadError_MismatchedType,
  DataReadError_InvalidEnumEntry,
  DataReadError_DuplicateEnumEntry,
  DataReadError_FieldNotFound,
  DataReadError_InvalidField,
  DataReadError_UnknownField,
  DataReadError_UnionTypeMissing,
  DataReadError_UnionTypeInvalid,
  DataReadError_UnionTypeUnsupported,
  DataReadError_UnionDataMissing,
  DataReadError_UnionDataInvalid,
  DataReadError_UnionUnknownField,
  DataReadError_UnionInvalidName,
  DataReadError_UnionNameNotSupported,
  DataReadError_NumberOutOfBounds,
  DataReadError_ZeroIsInvalid,
  DataReadError_EmptyStringIsInvalid,
  DataReadError_Base64DataInvalid,
  DataReadError_NullIsInvalid,
  DataReadError_EmptyArrayIsInvalid,
  DataReadError_ArrayLimitExceeded,
  DataReadError_NormalizationFailed,
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

/**
 * Read a data value from a binary blob.
 * NOTE: Data is left uninitialized in case of an error (does not require cleanup by the caller).
 * NOTE: Input needs to be at least 16 byte aligned to support external memory.
 *
 * Returns the remaining input.
 * The result is written to the given data memory.
 *
 * Pre-condition: data memory is big enough to hold a value with the given DataMeta.
 * Pre-condition: DataMeta definition is not modified in parallel with this call.
 * Pre-condition: res != null.
 */
String data_read_bin(const DataReg*, String, Allocator*, DataMeta, Mem data, DataReadResult*);

typedef struct {
  u32           protocolVersion;
  u32           checksum;         // crc32 (ISO 3309).
  u32           metaTypeNameHash; // Hash of the type's name.
  u32           metaFormatHash;   // Deep hash of the type's format ('data_hash()').
  DataContainer metaContainer : 8;
  DataFlags     metaFlags : 8;
  u16           metaFixedCount; // Size of fixed size containers (for example inline-array).
} DataBinHeader;

/**
 * Read the header from a binary blob.
 *
 * Returns the remaining input.
 * The result is written to the out pointer.
 *
 * Pre-condition: out != null.
 * Pre-condition: res != null.
 */
String data_read_bin_header(String, DataBinHeader* out, DataReadResult*);

/**
 * Compute the binary blob checksum (crc32 ISO 3309) over the given data.
 */
u32 data_read_bin_checksum(String);
