#pragma once
#include "core_string.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef enum {
  ZlibError_None,
  ZlibError_Truncated,
  ZlibError_Malformed,
  ZlibError_UnsupportedMethod,
  ZlibError_DeflateError,
  ZlibError_ChecksumError,
  ZlibError_Unknown,

  ZlibError_Count,
} ZlibError;

/**
 * Decode a ZLIB (RFC 1950) compressed data stream.
 *
 * Returns the remaining input.
 * The decoded data is written to the given DynString.
 */
String zlib_decode(String input, DynString* out, ZlibError*);
