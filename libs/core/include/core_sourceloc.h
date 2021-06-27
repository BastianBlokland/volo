#pragma once
#include "core_string.h"

/**
 * Information to identify a location in the source-code.
 */
typedef struct {
  String file;
  u32    line;
} SourceLoc;

/**
 * Return a String containing the current source-file path.
 */
#define source_file() string_lit(__FILE__)

/**
 * Return a 'u32' containing the current source line number.
 */
#define source_line() ((u32)(__LINE__))

/**
 * Create a 'SourceLoc' structure for the current source-location.
 */
#define source_location()                                                                          \
  ((SourceLoc){                                                                                    \
      .file = source_file(),                                                                       \
      .line = source_line(),                                                                       \
  })
