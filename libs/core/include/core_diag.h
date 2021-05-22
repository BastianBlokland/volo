#pragma once
#include "core_annotation.h"
#include "core_types.h"

/**
 * Information to identify a callsite in the source-code.
 */
typedef struct {
  const char* file;
  u32         line;
} CallSite;

/**
 * Return a 'const char*' containing the current source-file path.
 */
#define diag_file() (__FILE__)

/**
 * Return a 'u32' containing the current source line number.
 */
#define diag_line() ((u32)(__LINE__))

/**
 * Create a 'CallSite' structure for the current source-location.
 */
#define diag_callsite_create()                                                                     \
  ((CallSite){                                                                                     \
      .file = diag_file(),                                                                         \
      .line = diag_line(),                                                                         \
  })

/**
 * Fail the program with the message '_MSG_' if the given condition evaluates to false.
 */
#define diag_assert_msg(_CONDITION_, _MSG_)                                                        \
  do {                                                                                             \
    if (unlikely(!(_CONDITION_))) {                                                                \
      static CallSite callsite = diag_callsite_create();                                           \
      diag_assert_fail(&callsite, _MSG_);                                                          \
    }                                                                                              \
  } while (false)

/**
 * Fail the program if the given condition evaluates to false.
 */
#define diag_assert(_CONDITION_) diag_assert_msg(_CONDITION_, #_CONDITION_)

/**
 * Fail the compilation if given condition evaluates to false.
 */
#define diag_static_assert(_CONDITION_, _MSG_) _Static_assert(_CONDITION_, _MSG_)

/**
 * Log a message to the stdout stream.
 */
void diag_log(const char* format, ...);

/**
 * Log a message to the stderr stream.
 */
void diag_log_err(const char* format, ...);

/**
 * Indicate that an assertion has failed, logs the given message and exits the program.
 */
void diag_assert_fail(const CallSite*, const char* msg);

/**
 * Exit the program with exit code '1'.
 */
void diag_exit();
