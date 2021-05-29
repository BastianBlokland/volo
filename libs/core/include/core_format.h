#pragma once
#include "core_dynstring.h"

/**
 * Configuration struct for integer formatting.
 * Pre-condition: base > 1
 * Pre-condition: base <= 16
 */
typedef struct {

  /**
   * Base to write integers in. Eg. 10 for decimal, 2 for binary, 16 for hex.
   */
  u8 base;

} FormatIntOptions;

// clang-format off

/**
 * Write a value as ascii characters to the given dynamic-string.
 */
#define format_write(_DYNSTRING_, _VAL_, ...)                                                      \
  _Generic((_VAL_),                                                                                \
    u64: format_write_u64(_DYNSTRING_, _VAL_, &((FormatIntOptions){.base = 10, __VA_ARGS__})),     \
    i64: format_write_i64(_DYNSTRING_, _VAL_, &((FormatIntOptions){.base = 10, __VA_ARGS__}))      \
  )

// clang-format on

/**
 * Write a unsigned value as ascii characters.
 */
void format_write_u64(DynString*, u64 val, const FormatIntOptions*);

/**
 * Write a signed value as ascii characters.
 */
void format_write_i64(DynString*, i64 val, const FormatIntOptions*);
