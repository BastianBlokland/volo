#include "core_alloc.h"
#include "core_diag.h"

#include "result_internal.h"

CheckResult* check_result_create(Allocator* alloc) {
  CheckResult* result = alloc_alloc_t(alloc, CheckResult);
  *result             = (CheckResult){
      .alloc  = alloc,
      .errors = dynarray_create_t(alloc, CheckError, 0),
  };
  return result;
}

void check_result_destroy(CheckResult* result) {
  dynarray_for_t(&result->errors, CheckError, err, { string_free(result->alloc, err->msg); });
  dynarray_destroy(&result->errors);
  alloc_free_t(result->alloc, result);
}

void check_result_error(CheckResult* result, String msg, const SourceLoc source) {
  diag_assert_msg(!result->finished, "Result is already finished");

  *dynarray_push_t(&result->errors, CheckError) = (CheckError){
      .msg    = string_dup(result->alloc, msg),
      .source = source,
  };
}

void check_result_finish(CheckResult* result, const TimeDuration duration) {
  diag_assert_msg(!result->finished, "Result is already finished");
  diag_assert_msg(duration >= 0, "Negative duration {} is not valid", fmt_duration(duration));

  result->finished = true;
  result->duration = duration;
}
