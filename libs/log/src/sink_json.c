#include "core/alloc.h"
#include "core/diag.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/format.h"
#include "core/path.h"
#include "core/time.h"
#include "json/doc.h"
#include "json/write.h"
#include "log/sink.h"
#include "log/sink_json.h"

#include "logger_internal.h"

#define log_sink_buffer_size (16 * usize_kibibyte)

typedef struct {
  LogSink          api;
  Allocator*       alloc;
  File*            file;
  LogMask          mask;
  LogSinkJsonFlags flags;
} LogSinkJson;

static JsonVal log_to_json(JsonDoc* doc, const FormatArg* arg) {
  switch (arg->type) {
  case FormatArgType_i64:
    return json_add_number(doc, (f64)arg->value_i64);
  case FormatArgType_u64:
    return json_add_number(doc, (f64)arg->value_u64);
  case FormatArgType_f64:
    return json_add_number(doc, arg->value_f64);
  case FormatArgType_bool:
    return json_add_bool(doc, arg->value_bool);
  case FormatArgType_Size:
    return json_add_number(doc, (f64)arg->value_size);
  case FormatArgType_List: {
    const JsonVal arr = json_add_array(doc);
    for (const FormatArg* itr = arg->value_list; itr->type; ++itr) {
      json_add_elem(doc, arr, log_to_json(doc, itr));
    }
    return arr;
  }
  default:
    return json_add_string(doc, format_write_arg_scratch(arg));
  }
}

static void log_sink_json_write(
    LogSink*        sink,
    const LogLevel  lvl,
    const SourceLoc srcLoc,
    const TimeReal  timestamp,
    const String    message,
    const LogParam* params) {
  LogSinkJson* jsonSink = (LogSinkJson*)sink;
  if (!log_mask_enabled(jsonSink->mask, lvl)) {
    return;
  }

  JsonDoc*      doc  = json_create(g_allocScratch, 128);
  const JsonVal root = json_add_object(doc);

  json_add_field_lit(doc, root, "message", json_add_string(doc, message));
  json_add_field_lit(doc, root, "level", json_add_string(doc, log_level_str(lvl)));
  json_add_field_lit(
      doc, root, "timestamp", json_add_string(doc, format_write_arg_scratch(&fmt_time(timestamp))));
  json_add_field_lit(
      doc, root, "file", json_add_string(doc, format_write_arg_scratch(&fmt_path(srcLoc.file))));
  json_add_field_lit(doc, root, "line", json_add_number(doc, srcLoc.line));

  const JsonVal extra = json_add_object(doc);
  json_add_field_lit(doc, root, "extra", extra);

  for (const LogParam* itr = params; itr->arg.type; ++itr) {
    json_add_field_str(doc, extra, itr->name, log_to_json(doc, &itr->arg));
  }

  DynString str = dynstring_create_over(alloc_alloc(g_allocScratch, log_sink_buffer_size, 1));

  json_write(&str, doc, root, &json_write_opts(.mode = JsonWriteMode_Minimal));
  dynstring_append_char(&str, '\n');
  file_write_sync(jsonSink->file, dynstring_view(&str));

  dynstring_destroy(&str);
  json_destroy(doc);
}

static void log_sink_json_destroy(LogSink* sink) {
  LogSinkJson* jsonSink = (LogSinkJson*)sink;
  if (jsonSink->flags & LogSinkJsonFlags_DestroyFile) {
    file_destroy(jsonSink->file);
  }
  alloc_free_t(jsonSink->alloc, jsonSink);
}

LogSink*
log_sink_json(Allocator* alloc, File* file, const LogMask mask, const LogSinkJsonFlags flags) {
  LogSinkJson* sink = alloc_alloc_t(alloc, LogSinkJson);

  *sink = (LogSinkJson){
      .api   = {.write = log_sink_json_write, .destroy = log_sink_json_destroy},
      .alloc = alloc,
      .file  = file,
      .mask  = mask,
      .flags = flags,
  };

  return (LogSink*)sink;
}

LogSink* log_sink_json_to_path(Allocator* alloc, const LogMask mask, const String path) {
  File*      file;
  FileResult res;
  if ((res = file_create_dir_sync(path_parent(path))) != FileResult_Success) {
    diag_crash_msg("Failed to create parent directory: {}", fmt_text(file_result_str(res)));
  }
  const FileMode        mode   = FileMode_Create;
  const FileAccessFlags access = FileAccess_Write;
  if ((res = file_create(alloc, path, mode, access, &file)) != FileResult_Success) {
    diag_crash_msg("Failed to create log file: {}", fmt_text(file_result_str(res)));
  }
  return log_sink_json(alloc, file, mask, LogSinkJsonFlags_DestroyFile);
}

LogSink* log_sink_json_default(Allocator* alloc, const LogMask mask) {
  const String logPath = path_build_scratch(
      path_parent(g_pathExecutable),
      string_lit("logs"),
      path_name_timestamp_scratch(path_stem(g_pathExecutable), string_lit("log")));
  return log_sink_json_to_path(alloc, mask, logPath);
}
