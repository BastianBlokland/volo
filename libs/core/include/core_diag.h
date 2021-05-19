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

#define diag_assert_msg(_CONDITION_, _MSG_)                                                        \
  do {                                                                                             \
    if (unlikely(!(_CONDITION_))) {                                                                \
      static CallSite callsite = diag_callsite_init();                                             \
      diag_assert_fail(&callsite, _MSG_);                                                          \
    }                                                                                              \
  } while (false)

#define diag_assert(_CONDITION_) diag_assert_msg(_CONDITION_, #_CONDITION_)

#define diag_static_assert _Static_assert

void diag_log(const char* format, ...);
void diag_log_err(const char* format, ...);
void diag_assert_fail(const CallSite*, const char* msg);
void diag_exit();
