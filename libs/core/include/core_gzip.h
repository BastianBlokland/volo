#pragma once
#include "core_string.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef enum {
  GzipError_None,
  GzipError_Truncated,
  GzipError_Malformed,
  GzipError_UnsupportedMethod,
  GzipError_DeflateError,
  GzipError_ChecksumError,
  GzipError_Unknown,
} GzipError;

/**
 * Decode a GZIP (RFC 1952) compressed data stream.
 *
 * Returns the remaining input.
 * The decoded data is written to the given DynString.
 */
String gzip_decode(String input, DynString* out, GzipError*);
