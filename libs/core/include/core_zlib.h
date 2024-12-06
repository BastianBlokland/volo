#pragma once
#include "core.h"

typedef enum {
  ZlibError_None,
  ZlibError_Truncated,
  ZlibError_UnsupportedMethod,
  ZlibError_DeflateError,
  ZlibError_ChecksumError,

  ZlibError_Count,
} ZlibError;

/**
 * Return a textual representation of the given ZlibError.
 */
String zlib_error_str(ZlibError);

/**
 * Decode a ZLIB (RFC 1950) compressed data stream.
 *
 * Returns the remaining input.
 * The decoded data is written to the given DynString.
 */
String zlib_decode(String input, DynString* out, ZlibError*);
