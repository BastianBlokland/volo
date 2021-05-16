#pragma once
#include "core_likely.h"
#include "core_types.h"

typedef struct {
  const char* file;
  u32         line;
} CallSite;

#define diag_file() (__FILE__)
#define diag_line() (__LINE__)

#define diag_callsite_init()                                                                       \
  ((CallSite){                                                                                     \
      .file = diag_file(),                                                                         \
      .line = diag_line(),                                                                         \
  })

#define diag_assert_msg(condition, msg)                                                            \
  do {                                                                                             \
    if (unlikely(!(condition))) {                                                                  \
      static CallSite callsite = diag_callsite_init();                                             \
      diag_assert_fail(&callsite, msg);                                                            \
    }                                                                                              \
  } while (false)

#define diag_assert(condition) diag_assert_msg(condition, #condition)

void diag_log(const char* format, ...);
void diag_log_err(const char* format, ...);
void diag_assert_fail(const CallSite*, const char* msg);
void diag_exit();
