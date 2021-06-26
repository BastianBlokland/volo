#pragma once
#include "core_diag.h"
#include "core_string.h"

typedef u32 CheckBlockId;

typedef struct sCheckSpecContext  CheckSpecContext;
typedef struct sCheckBlockContext CheckBlockContext;

typedef struct {
  CheckBlockId id;
  String       description;
  DiagCallSite callSite;
} CheckBlock;

// clang-format off

/**
 * TODO: Document
 */
#define spec_name(_NAME_) _check_spec_##_NAME_

/**
 * TODO: Document
 */
#define spec(_NAME_) void spec_name(_NAME_)(MAYBE_UNUSED CheckSpecContext* _spec_ctx)

/**
 * TODO: Document
 */
#define it(_DESCRIPTION_)                                                                          \
  for (CheckBlockContext* _block_ctx = check_visit_block(                                          \
           _spec_ctx,                                                                              \
           (CheckBlock){                                                                           \
             .id          = (CheckBlockId)(__COUNTER__),                                           \
             .description = string_lit(_DESCRIPTION_),                                             \
             .callSite    = diag_callsite_create(),                                                \
           });                                                                                     \
       _block_ctx;                                                                                 \
       _block_ctx = null)

#define check_success()                                                                            \
  check_finish_success(_block_ctx)

#define check_failure()                                                                            \
  check_finish_failure(_block_ctx)

#define check_error(_MSG_FORMAT_LIT_, ...)                                                         \
  check_report_error(                                                                              \
    _block_ctx,                                                                                    \
    fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__),                                              \
    diag_callsite_create())

#define check(_CONDITION_, _MSG_FORMAT_LIT_, ...)                                                  \
  do {                                                                                             \
    if (UNLIKELY(!(_CONDITION_))) {                                                                \
      check_error(_MSG_FORMAT_LIT_, __VA_ARGS__);                                                  \
      check_failure();                                                                             \
    }                                                                                              \
  } while (false)

#define check_eq_int(_A_, _B_)                                                                     \
  check(_A_ == _B_, "{} != {}", fmt_int(_A_), fmt_int(_B_))

#define check_eq_string(_A_, _B_)                                                                  \
  check(                                                                                           \
    string_eq(_A_, _B_),                                                                           \
    "'{}' != '{}'",                                                                                \
    fmt_text(_A_, .flags = FormatTextFlags_EscapeNonPrintAscii),                                   \
    fmt_text(_B_, .flags = FormatTextFlags_EscapeNonPrintAscii))

// clang-format on

CheckBlockContext* check_visit_block(CheckSpecContext*, CheckBlock);
void               check_report_error(CheckBlockContext*, String msg, DiagCallSite);
NORETURN void      check_finish_failure(CheckBlockContext*);
NORETURN void      check_finish_success(CheckBlockContext*);
