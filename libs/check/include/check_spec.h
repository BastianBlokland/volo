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

typedef u32 CheckTestId;

typedef struct sCheckSpecContext CheckSpecContext;
typedef struct sCheckTestContext CheckTestContext;

typedef struct {
  CheckTestId    id;
  String         description;
  SourceLoc      source;
  CheckTestFlags flags;
} CheckTest;

// clang-format off

/**
 * Retrieve the name of a 'spec' routine.
 */
#define spec_name(_NAME_) _check_spec_##_NAME_

/**
 * Define a test specification function.
 * Specifications are containers that can include many tests.
 */
#define spec(_NAME_) void spec_name(_NAME_)(MAYBE_UNUSED CheckSpecContext* _specCtx)

/**
 * Define a skipped (will not execute) test.
 */
#define skip_it(_DESCRIPTION_, ...) it(_DESCRIPTION_, .flags = CheckTestFlags_Skip, __VA_ARGS__)

/**
 * Define a focussed test (will not execute any non-focussed tests.
 */
#define focus_it(_DESCRIPTION_, ...) it(_DESCRIPTION_, .flags = CheckTestFlags_Focus, __VA_ARGS__)

/**
 * Define a setup block.
 */
#define setup() if(check_visit_setup(_specCtx))

/**
 * Define a teardown block.
 */
#define teardown() if(check_visit_teardown(_specCtx))

/**
 * Define a test block.
 */
#define it(_DESCRIPTION_, ...)                                                                     \
  for (CheckTestContext* _testCtx = check_visit_test(                                              \
           _specCtx,                                                                               \
           (CheckTest){                                                                            \
             .id          = (CheckTestId)(__COUNTER__),                                            \
             .description = string_lit(_DESCRIPTION_),                                             \
             .source      = source_location(),                                                     \
             __VA_ARGS__                                                                           \
           });                                                                                     \
       _testCtx;                                                                                   \
       _testCtx = null)

/**
 * Early-out the current test run. Only valid inside test blocks.
 * If no error has occurred the test is considered passed otherwise its considered failed.
 */
#define check_early_out() check_finish(_testCtx)

/**
 * Report an error for the current test. Only valid inside test blocks.
 */
#define check_error(_MSG_FORMAT_LIT_, ...)                                                         \
  check_report_error(                                                                              \
    _testCtx,                                                                                      \
    fmt_write_scratch(_MSG_FORMAT_LIT_, __VA_ARGS__),                                              \
    source_location())

/**
 * Report an error if the given condition evaluates to false.
 */
#define check_msg(_CONDITION_, _MSG_FORMAT_LIT_, ...)                                              \
  do {                                                                                             \
    if (UNLIKELY(!(_CONDITION_))) {                                                                \
      check_error(_MSG_FORMAT_LIT_, __VA_ARGS__);                                                  \
    }                                                                                              \
  } while (false)

/**
 * Report an error and early-out the test if the given condition evaluates to false.
 */
#define check_require_msg(_CONDITION_, _MSG_FORMAT_LIT_, ...)                                      \
  do {                                                                                             \
    if (UNLIKELY(!(_CONDITION_))) {                                                                \
      check_error(_MSG_FORMAT_LIT_, __VA_ARGS__);                                                  \
      check_early_out();                                                                           \
    }                                                                                              \
  } while (false)

/**
 * Report an error if the given condition evaluates to false.
 */
#define check(_CONDITION_) check_msg(_CONDITION_, #_CONDITION_)

/**
 * Report an error and early-out the test if the given condition evaluates to false.
 */
#define check_require(_CONDITION_) check_require_msg(_CONDITION_, #_CONDITION_)

/**
  * Check if two integers are equal.
 */
#define check_eq_int(_A_, _B_)                                                                     \
  _Generic((_A_),                                                                                  \
    u8      : check_eq_u64_raw(_testCtx, (u64)(_A_), (u64)(_B_), source_location()),               \
    u16     : check_eq_u64_raw(_testCtx, (u64)(_A_), (u64)(_B_), source_location()),               \
    u32     : check_eq_u64_raw(_testCtx, (u64)(_A_), (u64)(_B_), source_location()),               \
    u64     : check_eq_u64_raw(_testCtx, (u64)(_A_), (u64)(_B_), source_location()),               \
    i8      : check_eq_i64_raw(_testCtx, (i64)(_A_), (i64)(_B_), source_location()),               \
    i16     : check_eq_i64_raw(_testCtx, (i64)(_A_), (i64)(_B_), source_location()),               \
    i32     : check_eq_i64_raw(_testCtx, (i64)(_A_), (i64)(_B_), source_location()),               \
    i64     : check_eq_i64_raw(_testCtx, (i64)(_A_), (i64)(_B_), source_location()),               \
    default : check_eq_i64_raw(_testCtx, (i64)(_A_), (i64)(_B_), source_location())                \
  )

/**
 * Check if two floats are within a certain threshold of eachother.
 */
#define check_eq_float(_A_, _B_, _THRESHOLD_)                                                      \
  check_eq_f64_raw(_testCtx, (_A_), (_B_), (_THRESHOLD_), source_location())

/**
 * Check if two strings are equal.
 */
#define check_eq_string(_A_, _B_)                                                                  \
  check_eq_string_raw(_testCtx, (_A_), (_B_), source_location())

// clang-format on

bool              check_visit_setup(CheckSpecContext*);
bool              check_visit_teardown(CheckSpecContext*);
CheckTestContext* check_visit_test(CheckSpecContext*, CheckTest);
void              check_report_error(CheckTestContext*, String msg, SourceLoc);
void              check_eq_u64_raw(CheckTestContext*, u64 a, u64 b, SourceLoc);
void              check_eq_i64_raw(CheckTestContext*, i64 a, i64 b, SourceLoc);
void              check_eq_f64_raw(CheckTestContext*, f64 a, f64 b, f64 threshold, SourceLoc);
void              check_eq_string_raw(CheckTestContext*, String a, String b, SourceLoc);
NORETURN void     check_finish(CheckTestContext*);
