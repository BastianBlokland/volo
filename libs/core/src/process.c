#include "core/array.h"
#include "core/diag.h"
#include "core/process.h"

static const String g_processResultStrs[] = {
    string_static("Success"),
    string_static("LimitReached"),
    string_static("TooManyArguments"),
    string_static("FailedToCreatePipe"),
    string_static("InvalidProcess"),
    string_static("NoPermission"),
    string_static("NotRunning"),
    string_static("ExecutableNotFound"),
    string_static("InvalidExecutable"),
    string_static("UnknownError"),
};

ASSERT(array_elems(g_processResultStrs) == ProcessResult_Count, "Incorrect number of strings");

String process_result_str(const ProcessResult result) {
  diag_assert(result < ProcessResult_Count);
  return g_processResultStrs[result];
}
