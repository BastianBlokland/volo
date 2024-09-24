#pragma once
#include "core_string.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef enum {
  DeflateError_None,
  DeflateError_Unknown,
} DeflateError;

/**
 * Decode (inflate) a DEFLATE (RFC 1951) compressed data stream.
 *
 * Returns the remaining input.
 * The decoded data is written to the given DynString.
 */
String deflate_decode(String input, DynString* out, DeflateError*);
