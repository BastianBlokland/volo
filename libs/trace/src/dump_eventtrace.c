#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_time.h"
#include "trace_dump.h"
#include "trace_sink_store.h"

/**
 * Dump all trace events in the Google EventTrace format.
 *
 * Spec: https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/edit
 */

typedef struct {
  DynString* out;
  ThreadId   pid;
  u64        processedThreads; // bit[64], indicates is the thread has been processed.
} DumpEventTraceCtx;

static void dump_eventtrace_init(DumpEventTraceCtx* ctx) {
  dynstring_append(ctx->out, string_lit("{\"displayTimeUnit\":\"ns\",\"traceEvents\":["));

  // Provide the process-name as a meta-data event.
  dynstring_append(ctx->out, string_lit("{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":"));
  format_write_u64(ctx->out, ctx->pid, &format_opts_int());
  dynstring_append(ctx->out, string_lit("\"args\":{\"name\":\""));
  dynstring_append(ctx->out, path_filename(g_path_executable));
  dynstring_append(ctx->out, string_lit("\"}}"));
}

static void dump_eventtrace_finalize(DumpEventTraceCtx* ctx) {
  String data = dynstring_view(ctx->out);

  // Replace the trailing comma with a closing array.
  if (*string_last(data) == ',') {
    *string_last(data) = ']';
  } else {
    dynstring_append_char(ctx->out, ']');
  }
  dynstring_append(ctx->out, string_lit("}\n"));
}

static void dump_eventtrace_color_write(DumpEventTraceCtx* ctx, const TraceColor color) {
  /**
   * Colors need to be one of the reserved colors.
   * https://github.com/catapult-project/catapult/blob/master/tracing/tracing/base/color_scheme.html
   */
  switch (color) {
  case TraceColor_Default:
  case TraceColor_White:
    dynstring_append(ctx->out, string_lit("\"white\""));
    break;
  case TraceColor_Red:
    dynstring_append(ctx->out, string_lit("\"yellow\""));
    break;
  case TraceColor_Green:
    dynstring_append(ctx->out, string_lit("\"olive\""));
    break;
  case TraceColor_Blue:
    dynstring_append(ctx->out, string_lit("\"grey\""));
    break;
  }
  diag_crash();
}

static void dump_eventtrace_visitor(
    TraceSink*             sink,
    void*                  userCtx,
    const u32              bufferIdx,
    const ThreadId         threadId,
    const String           threadName,
    const TraceStoreEvent* evt) {
  DumpEventTraceCtx* ctx = userCtx;

  if (UNLIKELY(bufferIdx > 64)) {
    diag_crash_msg("trace: Maximum thread-count exceeded");
  }
  if ((ctx->processedThreads & (u64_lit(1) << bufferIdx)) == 0) {

    // Provide the thread-name as a meta-data event.
    dynstring_append(ctx->out, string_lit("{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":"));
    format_write_u64(ctx->out, ctx->pid, &format_opts_int());
    dynstring_append(ctx->out, string_lit(",\"tid\":"));
    format_write_u64(ctx->out, threadId, &format_opts_int());
    dynstring_append(ctx->out, string_lit(",\"args\":{\"name\":\""));
    dynstring_append(ctx->out, threadName);
    dynstring_append(ctx->out, string_lit("\"}}"));

    ctx->processedThreads |= u64_lit(1) << bufferIdx;
  }

  const String id  = trace_sink_store_id(sink, evt->id);
  const String msg = mem_create(evt->msgData, evt->msgLength);

  /**
   * NOTE: What to use as the name and category is debatable, currently we prefer the message as the
   * name. Alternatively we could embed the message in the 'args' field.
   */
  const String name = string_is_empty(msg) ? id : msg;
  const String cat  = id;

  const u64 tsInMicroSeconds  = evt->timeStart / time_microsecond;
  const u64 durInMicroSeconds = (u64)evt->timeDur / time_microsecond;

  dynstring_append(ctx->out, string_lit("{\"name\":\""));
  dynstring_append(ctx->out, name);
  dynstring_append(ctx->out, string_lit("\",\"cat\":\""));
  dynstring_append(ctx->out, cat);
  dynstring_append(ctx->out, string_lit("\",\"ph\":\"X\",\"ts\":"));
  format_write_u64(ctx->out, tsInMicroSeconds, &format_opts_int());
  dynstring_append(ctx->out, string_lit(",\"dur\":"));
  format_write_u64(ctx->out, durInMicroSeconds, &format_opts_int());
  dynstring_append(ctx->out, string_lit(",\"pid\":"));
  format_write_u64(ctx->out, ctx->pid, &format_opts_int());
  dynstring_append(ctx->out, string_lit(",\"tid\":"));
  format_write_u64(ctx->out, threadId, &format_opts_int());
  dynstring_append(ctx->out, string_lit(",\"cname\":"));
  dump_eventtrace_color_write(ctx, evt->color);
  dynstring_append(ctx->out, string_lit("},"));
}

void trace_dump_eventtrace(TraceSink* storeSink, DynString* out) {
  DumpEventTraceCtx ctx = {
      .out = out,
      .pid = g_thread_pid,
  };

  dump_eventtrace_init(&ctx);
  trace_sink_store_visit(storeSink, dump_eventtrace_visitor, &ctx);
  dump_eventtrace_finalize(&ctx);
}

bool trace_dump_eventtrace_to_path(TraceSink* storeSink, String path) {
  DynString dynString = dynstring_create(g_alloc_heap, 128 * usize_kibibyte);

  trace_dump_eventtrace(storeSink, &dynString);

  const FileResult res = file_write_to_path_sync(path, dynstring_view(&dynString));

  dynstring_destroy(&dynString);

  return res == FileResult_Success;
}
