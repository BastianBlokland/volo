#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * File Monitor.
 * Can watch for file modifications.
 */
typedef struct sFileMonitor FileMonitor;

/**
 * Event that significes that the given file was modified.
 */
typedef struct {
  String path;
  u64    userData;
} FileMonitorEvent;

/**
 * FileMonitor result-code.
 */
typedef enum {
  FileMonitorResult_Success = 0,
  FileMonitorResult_AlreadyWatching,
  FileMonitorResult_WatchesLimitReached,
  FileMonitorResult_NoAccess,
  FileMonitorResult_PathTooLong,
  FileMonitorResult_PathInvalid,
  FileMonitorResult_UnknownError,

  FileMonitorResult_Count,
} FileMonitorResult;

/**
 * Return a textual representation of the given FileMonitorResult.
 */
String file_monitor_result_str(FileMonitorResult);

/**
 * Create a new file-monitor.
 * Destroy using 'file_monitor_destroy()'.
 */
FileMonitor* file_monitor_create(Allocator*);

/**
 * Destroy a file-monitor.
 */
void file_monitor_destroy(FileMonitor*);

/**
 * Watch the file at the given path for modifications.
 * NOTE: Poll for modification events using 'file_monitor_poll'.
 * NOTE: 'userData' is returned through the 'FileMonitorEvent' structure.
 */
FileMonitorResult file_monitor_watch(FileMonitor*, String path, u64 userData);

/**
 * Poll for file-monitor events.
 *
 * Returns:
 * 'true':  When an event was written to the output pointer.
 * 'false': When no more events are available at this time.
 */
bool file_monitor_poll(FileMonitor*, FileMonitorEvent* out);
