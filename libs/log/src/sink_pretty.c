#include "core/alloc.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/format.h"
#include "core/math.h"
#include "core/time.h"
#include "core/tty.h"
#include "log/sink.h"
#include "log/sink_pretty.h"

#include "logger.h"

#define log_sink_buffer_size (16 * usize_kibibyte)

typedef struct {
  LogSink            api;
  Allocator*         alloc;
  File*              file;
  LogMask            mask;
  bool               style;
  LogSinkPrettyFlags flags;
  TimeZone           timezone;
} LogSinkPretty;

static FormatArg arg_style_bold(LogSinkPretty* sink) {
  return sink->style ? fmt_ttystyle(.flags = TtyStyleFlags_Bold) : fmt_nop();
}

static FormatArg arg_style_dim(LogSinkPretty* sink) {
  return sink->style ? fmt_ttystyle(.flags = TtyStyleFlags_Faint) : fmt_nop();
}

static FormatArg arg_style_loglevel(LogSinkPretty* sink, const LogLevel lvl) {
  if (sink->style) {
    switch (lvl) {
    case LogLevel_Debug:
      return fmt_ttystyle(.fgColor = TtyFgColor_Blue);
    case LogLevel_Info:
      return fmt_ttystyle(.fgColor = TtyFgColor_Green);
    case LogLevel_Warn:
      return fmt_ttystyle(.fgColor = TtyFgColor_Yellow);
    case LogLevel_Error:
      return fmt_ttystyle(.fgColor = TtyFgColor_Red);
    default:
      return fmt_nop();
    }
  }
  return fmt_nop();
}

static FormatArg arg_style_reset(LogSinkPretty* sink) {
  return sink->style ? fmt_ttystyle() : fmt_nop();
}

static void log_sink_pretty_write(
    LogSink*        sink,
    const LogLevel  lvl,
    const SourceLoc srcLoc,
    const TimeReal  timestamp,
    const String    message,
    const LogParam* params) {
  (void)srcLoc;
  LogSinkPretty* prettySink = (LogSinkPretty*)sink;
  if (!log_mask_enabled(prettySink->mask, lvl)) {
    return;
  }

  DynString str = dynstring_create_over(alloc_alloc(g_allocScratch, log_sink_buffer_size, 1));

  fmt_write(
      &str,
      "{}{} {}{}[{}] {}{}\n",
      arg_style_dim(prettySink),
      fmt_time(
          timestamp,
          .terms    = FormatTimeTerms_Time | FormatTimeTerms_Milliseconds,
          .timezone = prettySink->timezone),
      arg_style_reset(prettySink),
      arg_style_loglevel(prettySink, lvl),
      fmt_text(log_level_str(lvl)),
      arg_style_reset(prettySink),
      fmt_text(message));

  usize maxParamNameWidth = 0;
  for (const LogParam* itr = params; itr->arg.type; ++itr) {
    maxParamNameWidth = math_max(maxParamNameWidth, itr->name.size);
  }

  for (const LogParam* itr = params; itr->arg.type; ++itr) {
    fmt_write(
        &str,
        "  {}: {}{}",
        fmt_text(itr->name),
        fmt_padding((u16)(maxParamNameWidth - itr->name.size)),
        arg_style_bold(prettySink));

    format_write_arg(&str, &itr->arg);
    fmt_write(&str, "{}\n", arg_style_reset(prettySink));
  }

  file_write_sync(prettySink->file, dynstring_view(&str));
  dynstring_destroy(&str);
}

static void log_sink_pretty_destroy(LogSink* sink) {
  LogSinkPretty* prettySink = (LogSinkPretty*)sink;
  if (prettySink->flags & LogSinkPrettyFlags_DestroyFile) {
    file_destroy(prettySink->file);
  }
  alloc_free_t(prettySink->alloc, prettySink);
}

LogSink*
log_sink_pretty(Allocator* alloc, File* file, const LogMask mask, const LogSinkPrettyFlags flags) {
  LogSinkPretty* sink = alloc_alloc_t(alloc, LogSinkPretty);

  *sink = (LogSinkPretty){
      .api      = {.write = log_sink_pretty_write, .destroy = log_sink_pretty_destroy},
      .alloc    = alloc,
      .file     = file,
      .mask     = mask,
      .style    = tty_isatty(file),
      .flags    = flags,
      .timezone = time_zone_current(),
  };

  return (LogSink*)sink;
}

LogSink* log_sink_pretty_default(Allocator* alloc, File* file, const LogMask mask) {
  return log_sink_pretty(alloc, file, mask, LogSinkPrettyFlags_Default);
}
