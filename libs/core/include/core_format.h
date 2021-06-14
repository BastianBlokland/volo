#pragma once
#include "core_bitset.h"
#include "core_dynstring.h"
#include "core_macro.h"
#include "core_time.h"
#include "core_tty.h"
#include "core_types.h"

typedef enum {
  FormatArgType_End = 0,
  FormatArgType_Nop,
  FormatArgType_List,
  FormatArgType_i64,
  FormatArgType_u64,
  FormatArgType_f64,
  FormatArgType_bool,
  FormatArgType_BitSet,
  FormatArgType_Mem,
  FormatArgType_Duration,
  FormatArgType_Time,
  FormatArgType_Size,
  FormatArgType_Text,
  FormatArgType_Path,
  FormatArgType_TtyStyle,
} FormatArgType;

/**
 * Type erased formatting argument.
 * Can be created with various helper macros, for example: 'fmt_int' or 'fmt_time'.
 */
typedef struct sFormatArg FormatArg;

struct sFormatArg {
  FormatArgType type;
  union {
    const FormatArg* value_list;
    i64              value_i64;
    u64              value_u64;
    f64              value_f64;
    bool             value_bool;
    BitSet           value_bitset;
    Mem              value_mem;
    TimeDuration     value_duration;
    TimeReal         value_time;
    usize            value_size;
    String           value_text;
    String           value_path;
    TtyStyle         value_ttystyle;
  };
  void* settings;
};

// clang-format off

/**
 * Create a No-Op formatting argument, will not output any characters.
 */
#define fmt_nop() ((FormatArg){ .type = FormatArgType_Nop })

/**
 * Create a tag argument to indicate the end of an argument list.
 */
#define fmt_end() ((FormatArg){0})

/**
 * Create a list formatting argument, takes a pointer to a FormatArg array that ends with with a
 * '0' (FormatArgType_End) argument.
 */
#define fmt_list(_CHILD_ARGS_, ...)                                                                \
  ((FormatArg){                                                                                    \
    .type       = FormatArgType_List,                                                              \
    .value_list = (_CHILD_ARGS_),                                                                  \
    .settings   = &format_opts_list(__VA_ARGS__)                                                   \
  })

/**
 * Create a list formatting argument.
 * Example usage:
 * '
 *  const FormatArg arg = fmt_list_lit(fmt_int(42), fmt_bool(true));
 * '
 */
#define fmt_list_lit(...)                                                                          \
  ((FormatArg){                                                                                    \
    .type       = FormatArgType_List,                                                              \
    .value_list = (const FormatArg[]){VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, (FormatArg){0})},       \
    .settings   = &format_opts_list()                                                              \
  })

/**
 * Create an integer formatting argument.
 */
#define fmt_int(_VAL_, ...)                                                                        \
  _Generic(+(_VAL_),                                                                               \
    u32: ((FormatArg){                                                                             \
      .type = FormatArgType_u64,                                                                   \
      .value_u64 = (u64)(_VAL_),                                                                   \
      .settings = &format_opts_int(__VA_ARGS__)                                                    \
    }),                                                                                            \
    i32: ((FormatArg){                                                                             \
      .type = FormatArgType_i64,                                                                   \
      .value_i64 = (i64)(_VAL_),                                                                   \
      .settings = &format_opts_int(__VA_ARGS__)                                                    \
    }),                                                                                            \
    u64: ((FormatArg){                                                                             \
      .type = FormatArgType_u64,                                                                   \
      .value_u64 = (u64)(_VAL_),                                                                   \
      .settings = &format_opts_int(__VA_ARGS__)                                                    \
    }),                                                                                            \
    i64: ((FormatArg){                                                                             \
      .type = FormatArgType_i64,                                                                   \
      .value_i64 = (i64)(_VAL_),                                                                   \
      .settings = &format_opts_int(__VA_ARGS__)                                                    \
    })                                                                                             \
  )

/**
 * Create an float formatting argument.
 */
#define fmt_float(_VAL_, ...)                                                                      \
  _Generic((_VAL_),                                                                                \
    f32: ((FormatArg){                                                                             \
      .type = FormatArgType_f64, .value_f64 = (_VAL_), .settings = &format_opts_float(__VA_ARGS__) \
    }),                                                                                            \
    f64: ((FormatArg){                                                                             \
      .type = FormatArgType_f64, .value_f64 = (_VAL_), .settings = &format_opts_float(__VA_ARGS__) \
    })                                                                                             \
  )

/**
 * Create an boolean formatting argument.
 */
#define fmt_bool(_VAL_) ((FormatArg){ .type = FormatArgType_bool, .value_bool = (_VAL_) })

/**
 * Create an bitset formatting argument.
 */
#define fmt_bitset(_VAL_) ((FormatArg){ .type = FormatArgType_BitSet, .value_bitset = (_VAL_) })

/**
 * Create an memory formatting argument.
 */
#define fmt_mem(_VAL_) ((FormatArg){ .type = FormatArgType_Mem, .value_mem = (_VAL_) })

/**
 * Create an byte size formatting argument.
 */
#define fmt_size(_VAL_) ((FormatArg){ .type = FormatArgType_Size, .value_size = (_VAL_) })

/**
 * Create an time duration formatting argument.
 */
#define fmt_duration(_VAL_) ((FormatArg){                                                          \
    .type = FormatArgType_Duration, .value_duration = (_VAL_)                                      \
  })

/**
 * Create an time real formatting argument.
 */
#define fmt_time(_VAL_, ...)                                                                       \
  ((FormatArg){                                                                                    \
      .type       = FormatArgType_Time,                                                            \
      .value_time = (_VAL_),                                                                       \
      .settings   = &format_opts_time(__VA_ARGS__)                                                 \
  })

/**
 * Create text formatting argument.
 */
#define fmt_text(_VAL_, ...)                                                                       \
  ((FormatArg){                                                                                    \
      .type       = FormatArgType_Text,                                                            \
      .value_text = (_VAL_),                                                                       \
      .settings   = &format_opts_text(__VA_ARGS__)                                                 \
  })

/**
 * Create text formatting argument from a string literal.
 */
#define fmt_text_lit(_VAL_) fmt_text(string_lit(_VAL_))

/**
 * Create file path formatting argument.
 */
#define fmt_path(_VAL_) ((FormatArg){ .type = FormatArgType_Path, .value_path = (_VAL_) })

/**
 * Create TtyStyle formatting argument.
 */
#define fmt_ttystyle(...)                                                                          \
  ((FormatArg){ .type = FormatArgType_TtyStyle, .value_ttystyle = (ttystyle(__VA_ARGS__)) })

/**
 * Create a array of format arguments.
 * Ends with with '0' (FormatArgType_End) argument.
 */
#define fmt_args(...) (const FormatArg[]){VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, (FormatArg){0})}

/**
 * Write a format string with arguments.
 * '{}' entries are replaced by arguments in order of appearance.
 * Supported format specifiers:
 * - '{>4}': Pad with spaces on the left side until a width of 4 chars is reached.
 * - '{<4}': Pad with spaces on the right side until a width of 4 chars is reached.
 * - '{:4}': Pad with spaces on both sides until a width of 4 chars is reached.
 */
#define fmt_write(_DYNSTRING_, _FORMAT_LIT_, ...)                                                  \
  format_write_formatted((_DYNSTRING_), string_lit(_FORMAT_LIT_), fmt_args(__VA_ARGS__))

/**
 * Create a formatted string in scratch memory. Meant for very short lived strings as the scratch
 * memory will be overwritten eventually.
 * Pre-condition: Formatted string fits in 2KiB.
 */
#define fmt_write_scratch(_FORMAT_LIT_, ...)                                                       \
  format_write_formatted_scratch(string_lit(_FORMAT_LIT_), fmt_args(__VA_ARGS__))

// clang-format on

/**
 * Configuration struct for formatting lists of arguments.
 */
typedef struct {
  String prefix;
  String suffix;
  String seperator;
} FormatOptsList;

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
 * Bit field of time terms.
 */
typedef enum {
  FormatTimeTerms_None         = 0,
  FormatTimeTerms_Date         = 1 << 0,
  FormatTimeTerms_Time         = 1 << 1,
  FormatTimeTerms_Milliseconds = 1 << 2,
  FormatTimeTerms_Timezone     = 1 << 3,
  FormatTimeTerms_All          = ~FormatTimeTerms_None,
} FormatTimeTerms;

/**
 * Configuration struct for time formatting.
 */
typedef struct {
  TimeZone        timezone;
  FormatTimeTerms terms;
} FormatOptsTime;

/**
 * Configuration flags for text formatting.
 */
typedef enum {
  FormatTextFlags_None                = 0,
  FormatTextFlags_EscapeNonPrintAscii = 1 << 0,
} FormatTextFlags;

/**
 * Configuration struct for text formatting.
 */
typedef struct {
  FormatTextFlags flags;
} FormatOptsText;

// clang-format off

#define format_opts_list(...)                                                                      \
  ((FormatOptsList){                                                                               \
    .prefix     = string_empty,                                                                    \
    .suffix     = string_empty,                                                                    \
    .seperator  = string_lit(", "),                                                                \
    __VA_ARGS__                                                                                    \
  })

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

#define format_opts_time(...)                                                                      \
  ((FormatOptsTime){                                                                               \
    .timezone   = time_zone_utc,                                                                   \
    .terms      = FormatTimeTerms_All,                                                             \
    __VA_ARGS__                                                                                    \
  })

#define format_opts_text(...)                                                                      \
  ((FormatOptsText){                                                                               \
    .flags   = FormatTextFlags_None,                                                               \
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
  _Generic((_VAL_),                                                                                \
    f32: format_write_f64(_DYNSTRING_, (f64)(_VAL_), &format_opts_float(__VA_ARGS__)),             \
    f64: format_write_f64(_DYNSTRING_, _VAL_, &format_opts_float(__VA_ARGS__))                     \
  )

// clang-format on

/**
 * Write a type-erased argument.
 */
void format_write_arg(DynString*, const FormatArg*);

/**
 * Write a format string with arguments.
 * '{}' entries are replaced by arguments in order of appearance.
 * Supported format specifiers:
 * - '{>4}': Pad with spaces on the left side until a width of 4 chars is reached.
 * - '{<4}': Pad with spaces on the right side until a width of 4 chars is reached.
 * - '{:4}': Pad with spaces on both sides until a width of 4 chars is reached.
 */
void format_write_formatted(DynString*, String format, const FormatArg* args);

/**
 * Create a formatted string in scratch memory. Meant for very short lived strings as the scratch
 * memory will be overwritten eventually.
 * Pre-condition: Formatted string fits in 2KiB.
 */
String format_write_formatted_scratch(String format, const FormatArg* args);

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
void format_write_mem(DynString*, Mem val);

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

/**
 * Write a size (in bytes) as human readable ascii characters.
 * Example output: '42.1MiB'.
 */
void format_write_size_pretty(DynString*, usize val);

/**
 * Write the text string.
 */
void format_write_text(DynString*, String val, const FormatOptsText*);

/**
 * Read all ascii whitespace at the beginning of the given string.
 * Returns the remaining input.
 * The whitespace slice is written to the output pointer, pass 'null' to ignore the output.
 */
String format_read_whitespace(String input, String* output);

/**
 * Read an unsigned 64 bit integer in the given base.
 * Returns the remaining input.
 * The value is written to the output pointer, pass 'null' to ignore the output.
 */
String format_read_u64(String input, u64* output, u8 base);

/**
 * Read a signed 64 bit integer in the given base.
 * Returns the remaining input.
 * The value is written to the output pointer, pass 'null' to ignore the output.
 */
String format_read_i64(String input, i64* output, u8 base);
