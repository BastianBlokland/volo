#pragma once
#include "core_annotation.h"
#include "core_format.h"
#include "core_types.h"

/**
 * Information to identify a callsite in the source-code.
 */
typedef struct {
  String file;
  u32    line;
} DiagCallSite;

/**
 * Return a String containing the current source-file path.
 */
#define diag_file() string_lit(__FILE__)

/**
 * Return a 'u32' containing the current source line number.
 */
#define diag_line() ((u32)(__LINE__))

/**
 * Create a 'DiagCallSite' structure for the current source-location.
 */
#define diag_callsite_create()                                                                     \
  ((DiagCallSite){                                                                                 \
      .file = diag_file(),                                                                         \
      .line = diag_line(),                                                                         \
  })

/**
 * Fail the program with the message '_MSG_' if the given condition evaluates to false.
 */
#define diag_assert_msg(_CONDITION_, _MSG_)                                                        \
  do {                                                                                             \
    if (unlikely(!(_CONDITION_))) {                                                                \
      diag_assert_fail(&diag_callsite_create(), _MSG_);                                            \
    }                                                                                              \
  } while (false)

/**
 * Fail the program if the given condition evaluates to false.
 */
#define diag_assert(_CONDITION_) diag_assert_msg(_CONDITION_, string_lit(#_CONDITION_))

/**
 * Log a message to the stdout stream.
 */
#define diag_log(_FORMAT_LIT_, ...)                                                                \
  diag_log_formatted(                                                                              \
      string_lit(_FORMAT_LIT_), (const FormatArg[]){__VA_ARGS__}, COUNT_VA_ARGS(__VA_ARGS__))

/**
 * Log a message to the stderr stream.
 */
#define diag_log_err(_FORMAT_LIT_, ...)                                                            \
  diag_log_err_formatted(                                                                          \
      string_lit(_FORMAT_LIT_), (const FormatArg[]){__VA_ARGS__}, COUNT_VA_ARGS(__VA_ARGS__))

/**
 * Log a message to the stdout stream.
 */
void diag_log_formatted(String format, const FormatArg* args, usize argsCount);

/**
 * Log a message to the stderr stream.
 */
void diag_log_err_formatted(String format, const FormatArg* args, usize argsCount);

/**
 * Indicate that an assertion has failed, logs the given message and crashes the program.
 */
VOLO_NORETURN void diag_assert_fail(const DiagCallSite*, String msg);

/**
 * Crash the program, will halt if running in a debugger.
 */
VOLO_NORETURN void diag_crash();
