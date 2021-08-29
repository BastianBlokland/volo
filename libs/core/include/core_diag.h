#pragma once
#include "core_annotation.h"
#include "core_format.h"
#include "core_sourceloc.h"
#include "core_types.h"

/**
 * Handler to be invoked when an assertion fails.
 * If 'true' is returned the assertion is ignored.
 * if 'false' is returned the application is terminated.
 */
typedef bool (*AssertHandler)(String msg, SourceLoc, void* context);

/**
 * Assert the given condition evaluates to true.
 */
#define diag_assert_msg(_CONDITION_, _MSG_FORMAT_LIT_, ...)                                        \
  do {                                                                                             \
    if (UNLIKELY(!(_CONDITION_))) {                                                                \
      diag_assert_fail(_MSG_FORMAT_LIT_, __VA_ARGS__);                                             \
    }                                                                                              \
  } while (false)

/**
 * Assert the given condition evaluates to true.
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
 * Report that an assertion has failed.
 */
#define diag_assert_fail(_MSG_FORMAT_LIT_, ...)                                                    \
  diag_assert_report_fail(fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__), source_location())

/**
 * Crash the program, will halt when running in a debugger.
 */
#define diag_crash_msg(_MSG_FORMAT_LIT_, ...)                                                      \
  diag_crash_msg_raw(fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__))

/**
 * Print a message to the stdout stream.
 */
void diag_print_raw(String msg);

/**
 * Print a message to the stderr stream.
 */
void diag_print_err_raw(String msg);

/**
 * Report that an assertion has failed.
 */
void diag_assert_report_fail(String msg, SourceLoc);

/**
 * Crash the program, will halt when running in a debugger.
 */
NORETURN void diag_crash();

/**
 * Crash the program, will halt when running in a debugger.
 */
NORETURN void diag_crash_msg_raw(String msg);

/**
 * Set the assert handler for the current thread.
 * If a assert handler is registered it is invoked whenever an assert is tripped.
 * 'context' is provided to the assert handler when its invoked.
 *
 * Note: Only a single assert handler can be registered per thread, the previous will be replaced.
 * Note: Invoke with 'null' to clear the current assert handler for this thread.
 */
void diag_set_assert_handler(AssertHandler, void* context);
