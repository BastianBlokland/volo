#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "core_time.h"
#include "log_sink.h"

#include "logger_internal.h"

#define log_sinks_max 6

struct sLogger {
  LogSink*       sinks[log_sinks_max];
  u32            sinkCount;
  ThreadSpinLock sinksLock;
  Allocator*     alloc;
};

Logger* g_logger = null;

static const String g_levelStrs[] = {
    string_static("dbg"),
    string_static("inf"),
    string_static("wrn"),
    string_static("err"),
};

ASSERT(array_elems(g_levelStrs) == LogLevel_Count, "Incorrect number of LogLevel strings");

static void log_destroy_sinks(Logger* logger) {
  for (u32 i = 0; i != logger->sinkCount; ++i) {
    LogSink* sink = logger->sinks[i];
    if (sink->destroy) {
      sink->destroy(sink);
    }
  }
}

static String log_format_text_scratch(const String str, const LogParam* params) {
  FormatArg fmtArgs[log_params_max + 1];
  usize     i = 0;
  for (; params[i].arg.type && i != log_params_max; ++i) {
    fmtArgs[i] = params[i].arg;
  }
  fmtArgs[i] = fmt_end();
  return format_write_formatted_scratch(str, fmtArgs);
}

String log_level_str(const LogLevel level) {
  diag_assert(level < LogLevel_Count);
  return g_levelStrs[level];
}

bool log_mask_enabled(const LogMask mask, const LogLevel level) {
  return (mask & (1 << level)) != 0;
}

void log_global_logger_init(void) {
  static Logger globalLogger = {0};
  g_logger                   = &globalLogger;
}

void log_global_logger_teardown(void) {
  log_destroy_sinks(g_logger);
  g_logger = null;
}

Logger* log_create(Allocator* alloc) {
  Logger* res = alloc_alloc_t(alloc, Logger);
  *res        = (Logger){.alloc = alloc};
  return res;
}

void log_destroy(Logger* logger) {
  diag_assert_msg(logger, "Logger not initialized");

  log_destroy_sinks(logger);
  alloc_free_t(logger->alloc, logger);
}

void log_add_sink(Logger* logger, LogSink* sink) {
  diag_assert_msg(logger, "Logger not initialized");
  diag_assert_msg(sink, "Invalid sink");

  thread_spinlock_lock(&logger->sinksLock);

  if (UNLIKELY(logger->sinkCount == log_sinks_max)) {
    diag_crash_msg("Maximum logger sink count exceeded");
  }

  logger->sinks[logger->sinkCount++] = sink;

  thread_spinlock_unlock(&logger->sinksLock);
}

void log_append(Logger* logger, LogLevel lvl, SourceLoc loc, String str, const LogParam* params) {
  diag_assert_msg(logger, "Logger not initialized");
  diag_assert_msg(!string_is_empty(str), "An empty message cannot logged");

  const String   message   = log_format_text_scratch(str, params);
  const TimeReal timestamp = time_real_clock();

  /**
   * Because sinks can only be added (not removed), we don't need to take the 'sinksLock' as the
   * worst that will happen is that the new sink won't be included for this entry yet.
   *
   * NOTE: This does mean that its important to keep the sinks array inline and not a dynamic array
   * that can be reallocated.
   */

  for (u32 i = 0; i != logger->sinkCount; ++i) {
    logger->sinks[i]->write(logger->sinks[i], lvl, loc, timestamp, message, params);
  }
}
