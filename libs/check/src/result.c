#include "core/alloc.h"
#include "core/diag.h"

#include "result_internal.h"

static const u32 g_checkMaxErrors = 1000;

CheckResult* check_result_create(Allocator* alloc) {
  CheckResult* result = alloc_alloc_t(alloc, CheckResult);

  *result = (CheckResult){
      .alloc  = alloc,
      .errors = dynarray_create_t(alloc, CheckError, 0),
  };

  return result;
}

void check_result_destroy(CheckResult* result) {
  dynarray_for_t(&result->errors, CheckError, err) { string_free(result->alloc, err->msg); }
  dynarray_destroy(&result->errors);
  alloc_free_t(result->alloc, result);
}

void check_result_error(CheckResult* result, String msg, const SourceLoc source) {
  if (UNLIKELY(result->finished)) {
    diag_crash_msg("Result is already finished");
  }
  static THREAD_LOCAL bool g_checkBusy;
  if (UNLIKELY(g_checkBusy)) {
    /**
     * Re-entered this function while reporting the error; system is likely out of memory.
     */
    result->errorsTruncated = true;
    return;
  }
  g_checkBusy = true;

  if (LIKELY(result->errors.size != g_checkMaxErrors)) {
    *dynarray_push_t(&result->errors, CheckError) = (CheckError){
        .msg    = string_dup(result->alloc, msg),
        .source = source,
    };
  } else {
    result->errorsTruncated = true;
  }

  g_checkBusy = false;
}

void check_result_finish(CheckResult* result, const TimeDuration duration) {
  diag_assert_msg(!result->finished, "Result is already finished");
  diag_assert_msg(duration >= 0, "Negative duration {} is not valid", fmt_duration(duration));

  result->finished = true;
  result->duration = duration;
}
