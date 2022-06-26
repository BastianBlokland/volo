#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_thread.h"
#include "core_time.h"
#include "log_sink.h"

#include "logger_internal.h"

typedef LogSink* LogSinkPtr;

struct sLogger {
  DynArray       sinks; // LogSink*[]
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
  dynarray_for_t(&logger->sinks, LogSinkPtr, sink) {
    if ((*sink)->destroy) {
      (*sink)->destroy(*sink);
    }
  }
  dynarray_destroy(&logger->sinks);
}

static String log_format_text_scratch(String str, const LogParam* params) {
  FormatArg fmtArgs[log_params_max + 1];
  usize     i = 0;
  for (; params[i].arg.type && i != log_params_max; ++i) {
    fmtArgs[i] = params[i].arg;
  }
  fmtArgs[i] = fmt_end();
  return format_write_formatted_scratch(str, fmtArgs);
}

String log_level_str(LogLevel level) {
  diag_assert(level < LogLevel_Count);
  return g_levelStrs[level];
}

bool log_mask_enabled(const LogMask mask, const LogLevel level) {
  return (mask & (1 << level)) != 0;
}

void log_global_logger_init() {
  static Logger globalLogger = {0};
  g_logger                   = &globalLogger;
  g_logger->sinks            = dynarray_create_t(g_alloc_heap, LogSink*, 2);
}

void log_global_logger_teardown() { log_destroy_sinks(g_logger); }

Logger* log_create(Allocator* alloc) {
  Logger* res = alloc_alloc_t(alloc, Logger);
  *res        = (Logger){
      .sinks = dynarray_create_t(alloc, LogSink*, 2),
      .alloc = alloc,
  };
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

  *dynarray_push_t(&logger->sinks, LogSink*) = sink;

  thread_spinlock_unlock(&logger->sinksLock);
}

void log_append(Logger* logger, LogLevel lvl, SourceLoc loc, String str, const LogParam* params) {
  diag_assert_msg(logger, "Logger not initialized");
  diag_assert_msg(!string_is_empty(str), "An empty message cannot logged");

  const String   message   = log_format_text_scratch(str, params);
  const TimeReal timestamp = time_real_clock();

  thread_spinlock_lock(&logger->sinksLock);

  dynarray_for_t(&logger->sinks, LogSinkPtr, sink) {
    (*sink)->write(*sink, lvl, loc, timestamp, message, params);
  }

  thread_spinlock_unlock(&logger->sinksLock);
}
