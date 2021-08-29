#pragma once
#include "core_dynstring.h"

#include "json_doc.h"

typedef enum {
  JsonWriteFlags_None   = 0,
  JsonWriteFlags_Pretty = 1 << 0,
} JsonWriteFlags;

/**
 * Formatting options for writing a json value.
 */
typedef struct {
  JsonWriteFlags flags;
  String         indent;
  String         newline;
} JsonWriteOpts;

/**
 * Formatting options for writing a json value.
 */
#define json_write_opts(...)                                                                       \
  ((JsonWriteOpts){                                                                                \
      .flags   = JsonWriteFlags_Pretty,                                                            \
      .indent  = string_lit("  "),                                                                 \
      .newline = string_lit("\n"),                                                                 \
      __VA_ARGS__})

/**
 * Write a json value.
 * Aims for compatiblity with rfc7159 json (https://datatracker.ietf.org/doc/html/rfc7159).
 *
 * Pre-condition: JsonVal is valid in the given document.
 */
void json_write(DynString*, const JsonDoc*, JsonVal, const JsonWriteOpts*);
