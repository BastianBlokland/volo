#pragma once
#include "core_annotation.h"
#include "core_format.h"
#include "core_math.h"
#include "core_sourceloc.h"
#include "core_string.h"

typedef enum {
  CheckTestFlags_None  = 0,
  CheckTestFlags_Skip  = 1 << 0,
  CheckTestFlags_Focus = 1 << 1,
} CheckTestFlags;

typedef u32 CheckBlockId;

typedef struct sCheckSpecContext CheckSpecContext;
typedef struct sCheckTestContext CheckTestContext;

typedef struct {
  CheckBlockId   id;
  String         description;
  SourceLoc      source;
  CheckTestFlags flags;
} CheckBlock;

// clang-format off

/**
 * TODO: Document
 */
#define spec_name(_NAME_) _check_spec_##_NAME_

/**
 * TODO: Document
 */
#define spec(_NAME_) void spec_name(_NAME_)(MAYBE_UNUSED CheckSpecContext* _specCtx)

#define skip_it(_DESCRIPTION_, ...) it(_DESCRIPTION_, .flags = CheckTestFlags_Skip, __VA_ARGS__)

#define focus_it(_DESCRIPTION_, ...) it(_DESCRIPTION_, .flags = CheckTestFlags_Focus, __VA_ARGS__)

/**
 * TODO: Document
 */
#define it(_DESCRIPTION_, ...)                                                                     \
  for (CheckTestContext* _testCtx = check_visit_block(                                             \
           _specCtx,                                                                               \
           (CheckBlock){                                                                           \
             .id          = (CheckBlockId)(__COUNTER__),                                           \
             .description = string_lit(_DESCRIPTION_),                                             \
             .source      = source_location(),                                                     \
             __VA_ARGS__                                                                           \
           });                                                                                     \
       _testCtx;                                                                                   \
       _testCtx = null)

#define check_early_out() check_finish(_testCtx)

#define check_error(_MSG_FORMAT_LIT_, ...)                                                         \
  check_report_error(                                                                              \
    _testCtx,                                                                                      \
    fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__),                                              \
    source_location())

#define check_msg(_CONDITION_, _MSG_FORMAT_LIT_, ...)                                              \
  do {                                                                                             \
    if (UNLIKELY(!(_CONDITION_))) {                                                                \
      check_error(_MSG_FORMAT_LIT_, __VA_ARGS__);                                                  \
      check_early_out();                                                                           \
    }                                                                                              \
  } while (false)

#define check(_CONDITION_) check_msg(_CONDITION_, #_CONDITION_)

#define check_eq_int(_A_, _B_)                                                                     \
  check_msg(_A_ == _B_, "{} == {}", fmt_int(_A_), fmt_int(_B_))

#define check_eq_float(_A_, _B_, _THRESHOLD_)                                                      \
  check_msg(math_abs(_A_ - _B_) < (_THRESHOLD_), "{} == {}", fmt_float(_A_), fmt_float(_B_))

#define check_eq_string(_A_, _B_)                                                                  \
  check_msg(                                                                                       \
    string_eq(_A_, _B_),                                                                           \
    "'{}' == '{}'",                                                                                \
    fmt_text(_A_, .flags = FormatTextFlags_EscapeNonPrintAscii),                                   \
    fmt_text(_B_, .flags = FormatTextFlags_EscapeNonPrintAscii))

// clang-format on

CheckTestContext* check_visit_block(CheckSpecContext*, CheckBlock);
void              check_report_error(CheckTestContext*, String msg, SourceLoc);
NORETURN void     check_finish(CheckTestContext*);
