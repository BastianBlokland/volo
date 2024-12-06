#pragma once
#include "log.h"

typedef enum {
  LogSinkPrettyFlags_None        = 0,
  LogSinkPrettyFlags_DestroyFile = 1 << 0,
} LogSinkPrettyFlags;

/**
 * PrettySink - sink that outputs as (styled) pretty printed text.
 * Especially useful for logging to the console.
 *
 * Example output::
 * ```
 * 2020-06-30T06:38:59.780823Z [info] Window opened
 *   width:  512
 *   height: 512
 * ```
 */

/**
 * Create a pretty log sink that outputs to the given file.
 * NOTE: Should be added to a logger using 'log_add_sink()'.
 * NOTE: Is automatically cleaned up when its parent logger is destroyed.
 * NOTE: Multiple writes can happen in parallel; make sure the file supports atomic writes.
 */
LogSink* log_sink_pretty(Allocator*, File*, LogMask, LogSinkPrettyFlags);

/**
 * Create a pretty log sink that outputs to the stdout pipe.
 * NOTE: Should be added to a logger using 'log_add_sink()'.
 * NOTE: Is automatically cleaned up when its parent logger is destroyed.
 */
LogSink* log_sink_pretty_default(Allocator*, LogMask);
