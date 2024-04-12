#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "trace_sink.h"

#include "tracer_internal.h"

#define trace_sinks_max 4
#define trace_message_max 64

struct sTracer {
  TraceSink*     sinks[trace_sinks_max];
  u32            sinkCount;
  ThreadSpinLock sinksLock;
  Allocator*     alloc;
};

Tracer* g_tracer = null;

static void trace_destroy_sinks(Tracer* tracer) {
  for (u32 i = 0; i != tracer->sinkCount; ++i) {
    TraceSink* sink = tracer->sinks[i];
    if (sink->destroy) {
      sink->destroy(sink);
    }
  }
}

void trace_global_tracer_init(void) {
  static Tracer globalTracer = {0};
  g_tracer                   = &globalTracer;
}

void trace_global_tracer_teardown(void) { trace_destroy_sinks(g_tracer); }

Tracer* trace_create(Allocator* alloc) {
  Tracer* res = alloc_alloc_t(alloc, Tracer);
  *res        = (Tracer){.alloc = alloc};
  return res;
}

void trace_destroy(Tracer* tracer) {
  diag_assert_msg(tracer, "Tracer not initialized");

  trace_destroy_sinks(tracer);
  alloc_free_t(tracer->alloc, tracer);
}

void trace_add_sink(Tracer* tracer, TraceSink* sink) {
  diag_assert_msg(tracer, "Trace system not initialized");
  diag_assert_msg(sink, "Invalid sink");

  thread_spinlock_lock(&tracer->sinksLock);

  if (UNLIKELY(tracer->sinkCount == trace_sinks_max)) {
    diag_crash_msg("Maximum trace sink count exceeded");
  }

  tracer->sinks[tracer->sinkCount++] = sink;

  thread_spinlock_unlock(&tracer->sinksLock);
}

void trace_event_begin(Tracer* tracer, const String id, const TraceColor color) {
  diag_assert_msg(tracer, "Tracer not initialized");
  diag_assert_msg(!string_is_empty(id), "Trace event-id cannot be empty");

  // No need to take the 'sinksLock' lock as sinks can only be added, never removed.
  for (u32 i = 0; i != tracer->sinkCount; ++i) {
    tracer->sinks[i]->eventBegin(tracer->sinks[i], id, color, string_empty);
  }
}

void trace_event_begin_msg(
    Tracer*          tracer,
    const String     id,
    const TraceColor color,
    const String     msg,
    const FormatArg* args) {
  diag_assert_msg(tracer, "Tracer not initialized");
  if (!tracer->sinkCount) {
    return;
  }

  DynString formatBuffer = dynstring_create_over(mem_stack(trace_message_max));
  format_write_formatted(&formatBuffer, msg, args);

  // No need to take the 'sinksLock' lock as sinks can only be added, never removed.
  for (u32 i = 0; i != tracer->sinkCount; ++i) {
    tracer->sinks[i]->eventBegin(tracer->sinks[i], id, color, dynstring_view(&formatBuffer));
  }
}

void trace_event_end(Tracer* tracer) {
  diag_assert_msg(tracer, "Tracer not initialized");

  // No need to take the 'sinksLock' lock as sinks can only be added, never removed.
  for (u32 i = 0; i != tracer->sinkCount; ++i) {
    tracer->sinks[i]->eventEnd(tracer->sinks[i]);
  }
}
