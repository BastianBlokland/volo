#pragma once
#include "core_format.h"
#include "core_sourceloc.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

typedef enum {
  LogLevel_Debug,
  LogLevel_Info,
  LogLevel_Warn,
  LogLevel_Error,

  LogLevel_Count,
} LogLevel;

typedef enum {
  LogMask_None  = 0,
  LogMask_Debug = 1 << LogLevel_Debug,
  LogMask_Info  = 1 << LogLevel_Info,
  LogMask_Warn  = 1 << LogLevel_Warn,
  LogMask_Error = 1 << LogLevel_Error,
  LogMask_All   = ~0,
} LogMask;

/**
 * Structured logging parameter (key-value pair).
 */
typedef struct {
  String    name;
  FormatArg arg;
} LogParam;

/**
 * Logger object.
 */
typedef struct sLogger Logger;

/**
 * Output sink for log messages.
 */
typedef struct sLogSink LogSink;

/**
 * Construct a logging parameter (key-value pair).
 * Name: String literal.
 * Arg: Formatting argument (see FormatArg in 'core_format.h').
 */
#define log_param(_NAME_LIT_, _ARG_) ((LogParam){.name = string_lit(_NAME_LIT_), .arg = (_ARG_)})

/**
 * Construct a collection of logging parameters.
 */
#define log_params(...)                                                                            \
  (const LogParam[]) { VA_ARGS_SKIP_FIRST(0, ##__VA_ARGS__, (LogParam){.name = {0}, .arg = {0}}) }

/**
 * Append a log message to the given logger object.
 */
#define log(_LOGGER_, _LVL_, _TXT_LIT_, ...)                                                       \
  do {                                                                                             \
    Logger* _logger_ = (_LOGGER_);                                                                 \
    if (_logger_) {                                                                                \
      log_append(                                                                                  \
          (_logger_), (_LVL_), source_location(), string_lit(_TXT_LIT_), log_params(__VA_ARGS__)); \
    }                                                                                              \
  } while (false)

#ifndef VOLO_FAST
#define log_d(_TXT_LIT_, ...) log(g_logger, LogLevel_Debug, _TXT_LIT_, __VA_ARGS__)
#else
#define log_d(_TXT_LIT_, ...)
#endif

#define log_i(_TXT_LIT_, ...) log(g_logger, LogLevel_Info, _TXT_LIT_, __VA_ARGS__)
#define log_w(_TXT_LIT_, ...) log(g_logger, LogLevel_Warn, _TXT_LIT_, __VA_ARGS__)
#define log_e(_TXT_LIT_, ...) log(g_logger, LogLevel_Error, _TXT_LIT_, __VA_ARGS__)

/**
 * Global logger.
 */
extern Logger* g_logger;

/**
 * Create a new logger object.
 * Should be destroyed using 'log_destroy'.
 */
Logger* log_create(Allocator*);

/**
 * Destroy a logger object.
 */
void log_destroy(Logger*);

/**
 * Add a new sink to the specified logger object.
 * NOTE: Sinks are automatically destroyed when the logger object is destroyed.
 */
void log_add_sink(Logger*, LogSink*);

/**
 * Append a new message to the given logger.
 *
 * Pre-condition: !string_is_empty(text).
 * Pre-condition: 'params' array should be terminated with an empty logging params.
 */
void log_append(Logger*, LogLevel, SourceLoc, String text, const LogParam* params);
