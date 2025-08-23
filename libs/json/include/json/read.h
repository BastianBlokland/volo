#pragma once
#include "json/forward.h"

typedef enum {
  JsonResultType_Success,
  JsonResultType_Fail,
} JsonResultType;

typedef enum {
  JsonError_DuplicateField,
  JsonError_InvalidChar,
  JsonError_InvalidCharInFalse,
  JsonError_InvalidCharInNull,
  JsonError_InvalidCharInString,
  JsonError_InvalidCharInTrue,
  JsonError_InvalidUtf8,
  JsonError_InvalidEscapeSequence,
  JsonError_InvalidFieldName,
  JsonError_InvalidFieldSeparator,
  JsonError_MaximumDepthExceeded,
  JsonError_TooLongString,
  JsonError_Truncated,
  JsonError_UnexpectedToken,
  JsonError_UnterminatedString,

  JsonError_Count,
} JsonError;

/**
 * Result of parsing a json value.
 * If 'type == JsonResultType_Success' then 'val' contains a value in the provided JsonDoc.
 * else 'error' contains the reason why parsing failed.
 */
typedef struct {
  JsonResultType type;
  union {
    JsonVal   val;
    JsonError error;
  };
} JsonResult;

typedef enum {
  JsonReadFlags_None               = 0,
  JsonReadFlags_HashOnlyFieldNames = 1 << 0, // Do not store object field names as strings.
} JsonReadFlags;

/**
 * Return a textual representation of the given JsonError.
 */
String json_error_str(JsonError);

/**
 * Read a json value.
 * Aims for compatiblity with rfc7159 json (https://datatracker.ietf.org/doc/html/rfc7159).
 *
 * Returns the remaining input.
 * The result is written to the output pointer.
 *
 * Pre-condition: res != null.
 */
String json_read(JsonDoc*, String, JsonReadFlags, JsonResult* res);
