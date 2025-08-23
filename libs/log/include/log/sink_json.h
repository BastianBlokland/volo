#pragma once
#include "log/forward.h"

typedef enum {
  LogSinkJsonFlags_None        = 0,
  LogSinkJsonFlags_DestroyFile = 1 << 0,
} LogSinkJsonFlags;

/**
 * JsonSink - sink that outputs as structured json objects.
 * Especially useful for processing the logs using external tools.
 *
 * For example to print the severity and the message for each log:
 * $ cat app.log | jq '{ level: .level,  msg: .message }'
 *
 * Or print all errors and warnings:
 * $ cat app.log | jq 'select((.level == "err") or (.level == "warn"))'
 *
 * Or printing the 'width' and the 'height' param for every resize log:
 * $ cat app.log | jq 'select(.message == "Resized") | { w: .extra.width, h: .extra.height }'
 *
 * Or follow a 'live' log:
 * $ tail --follow app.log | jq '.message'
 *
 * Example output (without the newlines and the spaces):
 * ```
 * {
 *   "message": "Example",
 *   "level": "info",
 *   "timestamp": "2020-06-29T05:49:07.401231Z",
 *   "file": "/path/main.c",
 *   "line": 16,
 *   "extra": { "val": 42 }
 * }
 * ```
 */

/**
 * Create a json log sink that outputs to the given file.
 * NOTE: Should be added to a logger using 'log_add_sink()'.
 * NOTE: Is automatically cleaned up when its parent logger is destroyed.
 * NOTE: Multiple writes can happen in parallel; make sure the file supports atomic writes.
 */
LogSink* log_sink_json(Allocator*, File*, LogMask, LogSinkJsonFlags);

/**
 * Create a json log sink that writes a file at the given path.
 * NOTE: Should be added to a logger using 'log_add_sink()'.
 * NOTE: Is automatically cleaned up when its parent logger is destroyed.
 */
LogSink* log_sink_json_to_path(Allocator*, LogMask, String path);

/**
 * Create a json log sink that writes a file called '[executable-name]_[timestamp].log' in a
 * directory called 'logs' next to the executable.
 *
 * NOTE: Should be added to a logger using 'log_add_sink()'.
 * NOTE: Is automatically cleaned up when its parent logger is destroyed.
 */
LogSink* log_sink_json_default(Allocator*, LogMask);
