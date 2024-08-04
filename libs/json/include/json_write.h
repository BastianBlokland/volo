#pragma once
#include "core_dynstring.h"
#include "json_doc.h"

typedef enum {
  JsonWriteMode_Minimal,
  JsonWriteMode_Compact,
  JsonWriteMode_Verbose,
} JsonWriteMode;

typedef enum {
  JsonWriteFlags_None             = 0,
  JsonWriteFlags_EscapeDollarSign = 1 << 0,
} JsonWriteFlags;

/**
 * Formatting options for writing a json value.
 */
typedef struct {
  JsonWriteMode  mode : 8;
  JsonWriteFlags flags : 8;
  u8             numberMaxDecDigits;
  f64            numberExpThresholdPos;
  f64            numberExpThresholdNeg;
  String         indent;
  String         newline;
} JsonWriteOpts;

/**
 * Formatting options for writing a json value.
 */
#define json_write_opts(...)                                                                       \
  ((JsonWriteOpts){                                                                                \
      .mode                  = JsonWriteMode_Minimal,                                              \
      .flags                 = JsonWriteFlags_None,                                                \
      .numberMaxDecDigits    = 10,                                                                 \
      .numberExpThresholdPos = 1e20,                                                               \
      .numberExpThresholdNeg = 1e-5,                                                               \
      .indent                = string_lit("  "),                                                   \
      .newline               = string_lit("\n"),                                                   \
      __VA_ARGS__})

/**
 * Write a json value.
 * Aims for compatiblity with rfc7159 json (https://datatracker.ietf.org/doc/html/rfc7159).
 *
 * Pre-condition: JsonVal is valid in the given document.
 */
void json_write(DynString*, const JsonDoc*, JsonVal, const JsonWriteOpts*);
