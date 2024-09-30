#pragma once
#include "core_string.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

// Forward declare from 'core_time.h'.
typedef i64 TimeReal;

typedef enum {
  GzipError_None,
  GzipError_Truncated,
  GzipError_Malformed,
  GzipError_UnsupportedMethod,
  GzipError_DeflateError,
  GzipError_ChecksumError,
  GzipError_Unknown,
} GzipError;

typedef struct {
  String   name;
  TimeReal modTime;
} GzipMeta;

/**
 * Decode a GZIP (RFC 1952) compressed data stream.
 * Optionally retrieves meta-data about the gzip file (provide null when not needed).
 *
 * Returns the remaining input.
 * The decoded data is written to the given DynString.
 */
String gzip_decode(String input, GzipMeta* outMeta, DynString* out, GzipError*);
