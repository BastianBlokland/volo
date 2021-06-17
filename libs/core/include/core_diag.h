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
#define diag_assert_msg(_CONDITION_, _MSG_FORMAT_LIT_, ...)                                        \
  do {                                                                                             \
    if (UNLIKELY(!(_CONDITION_))) {                                                                \
      diag_assert_fail(_MSG_FORMAT_LIT_, __VA_ARGS__);                                             \
    }                                                                                              \
  } while (false)

/**
 * Fail the program if the given condition evaluates to false.
 */
#define diag_assert(_CONDITION_) diag_assert_msg(_CONDITION_, #_CONDITION_)

/**
 * Print a message to the stdout stream.
 */
#define diag_print(_MSG_FORMAT_LIT_, ...)                                                          \
  diag_print_raw(fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__))

/**
 * Print a message to the stderr stream.
 */
#define diag_print_err(_MSG_FORMAT_LIT_, ...)                                                      \
  diag_print_err_raw(fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__))

/**
 * Indicate that an assertion has failed, print the given message and crashes the program.
 */
#define diag_assert_fail(_MSG_FORMAT_LIT_, ...)                                                    \
  diag_assert_fail_raw(&diag_callsite_create(), fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__))

/**
 * Print a message to the stdout stream.
 */
void diag_print_raw(String msg);

/**
 * Print a message to the stderr stream.
 */
void diag_print_err_raw(String msg);

/**
 * Indicate that an assertion has failed, print the given message and crashes the program.
 */
NORETURN void diag_assert_fail_raw(const DiagCallSite*, String msg);

/**
 * Crash the program, will halt if running in a debugger.
 */
NORETURN void diag_crash();
