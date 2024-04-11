#include "core_diag.h"
#include "core_thread.h"
#include "trace_sink.h"

#include "tracer_internal.h"

#define trace_sinks_max 4
#define trace_message_max 64

static bool           g_traceInitialized;
static TraceSink*     g_traceSinks[trace_sinks_max];
static u32            g_traceSinkCount;
static ThreadSpinLock g_traceSinksLock;

void trace_global_tracer_init(void) { g_traceInitialized = true; }

void trace_global_tracer_teardown(void) {
  // Destroy all the sinks.
  for (u32 i = 0; i != g_traceSinkCount; ++i) {
    if (g_traceSinks[i]->destroy) {
      g_traceSinks[i]->destroy(g_traceSinks[i]);
    }
  }
}

void trace_add_sink(TraceSink* sink) {
  diag_assert_msg(g_traceInitialized, "Trace system not initialized");
  diag_assert_msg(sink, "Invalid sink");

  thread_spinlock_lock(&g_traceSinksLock);

  if (UNLIKELY(g_traceSinkCount == trace_sinks_max)) {
    diag_crash_msg("Maximum trace sink count exceeded");
  }

  g_traceSinks[g_traceSinkCount++] = sink;

  thread_spinlock_unlock(&g_traceSinksLock);
}

void trace_tracer_begin(const String id, const TraceColor color) {
  // No need to take the 'g_traceSinksLock' lock as sinks can only be added, never removed.
  for (u32 i = 0; i != g_traceSinkCount; ++i) {
    g_traceSinks[i]->eventBegin(g_traceSinks[i], id, color, string_empty);
  }
}

void trace_tracer_begin_msg(
    const String id, const TraceColor color, const String msg, const FormatArg* args) {
  if (!g_traceSinkCount) {
    return;
  }

  DynString formatBuffer = dynstring_create_over(mem_stack(trace_message_max));
  format_write_formatted(&formatBuffer, msg, args);

  // No need to take the 'g_traceSinksLock' lock as sinks can only be added, never removed.
  for (u32 i = 0; i != g_traceSinkCount; ++i) {
    g_traceSinks[i]->eventBegin(g_traceSinks[i], id, color, dynstring_view(&formatBuffer));
  }
}

void trace_tracer_end() {
  // No need to take the 'g_traceSinksLock' lock as sinks can only be added, never removed.
  for (u32 i = 0; i != g_traceSinkCount; ++i) {
    g_traceSinks[i]->eventEnd(g_traceSinks[i]);
  }
}
