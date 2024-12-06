#pragma once
#include "core_string.h"

typedef enum {
  DeflateError_None,
  DeflateError_Malformed,
  DeflateError_Truncated,
} DeflateError;

/**
 * Decode (inflate) a DEFLATE (RFC 1951) compressed data stream.
 *
 * Returns the remaining input.
 * The decoded data is written to the given DynString.
 */
String deflate_decode(String input, DynString* out, DeflateError*);
