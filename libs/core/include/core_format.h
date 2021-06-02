#pragma once
#include "core_bitset.h"
#include "core_dynstring.h"
#include "core_time.h"

/**
 * Configuration struct for integer formatting.
 */
typedef struct {

  /**
   * Base to write integers in. Eg. 10 for decimal, 2 for binary, 16 for hex.
   * Condition: base > 1
   * Condition: base <= 16
   */
  u8 base;

  /**
   * Minimum amount of digits to write.
   * For example writing 42 with minDigits of 4 results in: '0042'.
   */
  u8 minDigits;

} FormatOptsInt;

/**
 * Configuration struct for floating point formatting.
 */
typedef struct {

  /**
   * Minimum amount of digits after the decimal place.
   */
  u8 minDecDigits;

  /**
   * Maximum amount of digits after the decimal place (will apply rounding to the remainder).
   */
  u8 maxDecDigits;

  /**
   * Use scientific notation for values bigger then this.
   */
  f64 expThresholdPos;

  /**
   * Use scientific notation for values closer to 0 then this.
   */
  f64 expThresholdNeg;

} FormatOptsFloat;

/**
 * Configuration struct for time formatting.
 */
typedef struct {

  /**
   * Local timezone to format the time for.
   */
  TimeZone timezone;

  /**
   * Include milliseconds.
   */
  bool milliseconds;

} FormatOptsTime;

// clang-format off

#define format_opts_int(...)                                                                       \
  ((FormatOptsInt){                                                                                \
    .base      = 10,                                                                               \
    .minDigits = 0,                                                                                \
    __VA_ARGS__                                                                                    \
  })

#define format_opts_float(...)                                                                     \
  ((FormatOptsFloat){                                                                              \
    .minDecDigits     = 0,                                                                         \
    .maxDecDigits     = 7,                                                                         \
    .expThresholdPos  = 1e7,                                                                       \
    .expThresholdNeg  = 1e-5,                                                                      \
    __VA_ARGS__                                                                                    \
  })

#define format_opts_time(...)                                                                  \
  ((FormatOptsTime){                                                                           \
    .timezone     = time_zone_utc,                                                               \
    .milliseconds = true,                                                                          \
    __VA_ARGS__                                                                                    \
  })

/**
 * Write a integer as ascii characters to the given dynamic-string.
 */
#define format_write_int(_DYNSTRING_, _VAL_, ...)                                                  \
  _Generic(+(_VAL_),                                                                               \
    u32: format_write_u64(_DYNSTRING_, (u64)(_VAL_), &format_opts_int(__VA_ARGS__)),               \
    i32: format_write_i64(_DYNSTRING_, (i64)(_VAL_), &format_opts_int(__VA_ARGS__)),               \
    u64: format_write_u64(_DYNSTRING_, _VAL_, &format_opts_int(__VA_ARGS__)),                      \
    i64: format_write_i64(_DYNSTRING_, _VAL_, &format_opts_int(__VA_ARGS__))                       \
  )

/**
 * Write a floating point number as ascii characters to the given dynamic-string.
 */
#define format_write_float(_DYNSTRING_, _VAL_, ...)                                                \
  _Generic(+(_VAL_),                                                                               \
    f32: format_write_f64(_DYNSTRING_, (f64)(_VAL_), &format_opts_float(__VA_ARGS__)),             \
    f64: format_write_f64(_DYNSTRING_, _VAL_, &format_opts_float(__VA_ARGS__))                     \
  )

// clang-format on

/**
 * Write a unsigned value as ascii characters.
 */
void format_write_u64(DynString*, u64 val, const FormatOptsInt*);

/**
 * Write a signed value as ascii characters.
 */
void format_write_i64(DynString*, i64 val, const FormatOptsInt*);

/**
 * Write a floating point value as ascii characters.
 */
void format_write_f64(DynString*, f64 val, const FormatOptsFloat*);

/**
 * Write a boolean value as ascii characters.
 */
void format_write_bool(DynString*, bool val);

/**
 * Write a bitset value as ascii characters (0 for unset bits or 1 for set bits).
 */
void format_write_bitset(DynString*, BitSet val);

/**
 * Write a mem value as hexadecimal ascii characters.
 */
void format_write_mem(DynString*, BitSet val);

/**
 * Write a duration as human readable ascii characters.
 * Example output: '42.3s'.
 */
void format_write_time_duration_pretty(DynString*, TimeDuration val);

/**
 * Date and time in iso-8601 format (https://en.wikipedia.org/wiki/ISO_8601).
 * Example output: 1920-03-19T07:11:23+02:00.
 * Example output: 1920-03-19T07:11:23Z (utc timezone).
 * Example output: 1920-03-19T07:11:23.323+02:00. (including milliseconds)
 */
void format_write_time_iso8601(DynString*, TimeReal val, const FormatOptsTime*);
