#include "core_array.h"
#include "core_diag.h"
#include "core_file_monitor.h"

static const String g_fileMonitorResultStrs[] = {
    string_static("FileMonitorSuccess"),
    string_static("FileMonitorAlreadyWatching"),
    string_static("FileMonitorWatchesLimitReached"),
    string_static("FileMonitorNoAccess"),
    string_static("FileMonitorPathTooLong"),
    string_static("FileMonitorFileDoesNotExist"),
    string_static("FileMonitorUnableToOpenRoot"),
    string_static("FileMonitorUnknownError"),
};

ASSERT(
    array_elems(g_fileMonitorResultStrs) == FileMonitorResult_Count,
    "Incorrect number of FileMonitorResult strings");

String file_monitor_result_str(const FileMonitorResult result) {
  diag_assert(result < FileMonitorResult_Count);
  return g_fileMonitorResultStrs[result];
}
