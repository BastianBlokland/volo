#pragma once
#include "core.h"
#include "core_format.h"
#include "core_sourceloc.h"

typedef bool (*AssertHandler)(String msg, SourceLoc, void* context);
typedef void (*CrashHandler)(String msg, void* context);

/**
 * Assert the given condition evaluates to true.
 */
#ifndef VOLO_FAST
#define diag_assert_msg(_CONDITION_, _MSG_FORMAT_LIT_, ...)                                        \
  do {                                                                                             \
    if (UNLIKELY(!(_CONDITION_))) {                                                                \
      diag_assert_fail(_MSG_FORMAT_LIT_, __VA_ARGS__);                                             \
    }                                                                                              \
  } while (false)
#else
#define diag_assert_msg(_CONDITION_, _MSG_FORMAT_LIT_, ...)
#endif

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
#ifndef VOLO_FAST
#define diag_assert_fail(_MSG_FORMAT_LIT_, ...)                                                    \
  diag_assert_report_fail(fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__), source_location())
#else
#define diag_assert_fail(_MSG_FORMAT_LIT_, ...)
#endif

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
 * Halt the program when running with a debugger attached.
 */
void diag_break(void);

/**
 * Crash the program.
 */
NORETURN void diag_crash(void);

/**
 * Crash the program.
 */
NORETURN void diag_crash_msg_raw(String msg);

/**
 * Set the assert handler for the current thread.
 * When the handler returns true: the assertion is ignored else the application is terminated.
 *
 * NOTE: 'context' is provided to the assert handler when its invoked.
 * NOTE: Only a single assert handler can be registered per thread, the previous will be replaced.
 * NOTE: Invoke with 'null' to clear the current assert handler for this thread.
 */
void diag_assert_handler(AssertHandler, void* context);

/**
 * Set the application crash handler.
 * The handler is invoked when a crash is reported. Crashes are always fatal, the handler cannot
 * prevent application shutdown. Care be taken while writing a crash-handler as the application is
 * in an unknown state.
 *
 * NOTE: 'context' is provided to the crash handler when its invoked.
 * NOTE: Only a single crash handler can be registered, the previous will be replaced.
 * NOTE: Invoke with 'null' to clear the current crash handler.
 */
void diag_crash_handler(CrashHandler, void* context);
