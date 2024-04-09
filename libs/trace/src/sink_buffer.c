#include "core_alloc.h"
#include "core_diag.h"
#include "trace_sink_buffer.h"

#include "event_internal.h"

/**
 * Trace sink implementation that stores events in in-memory buffers to be queried later.
 */

typedef struct {
  TraceSink  api;
  Allocator* alloc;
} TraceSinkBuffer;

static void trace_sink_buffer_event_begin(
    TraceSink* sink, const String id, const TraceColor color, const String msg) {
  TraceSinkBuffer* sinkBuffer = (TraceSinkBuffer*)sink;
  (void)sinkBuffer;
  (void)id;
  (void)color;
  (void)msg;
}

static void trace_sink_buffer_event_end(TraceSink* sink) {
  TraceSinkBuffer* sinkBuffer = (TraceSinkBuffer*)sink;
  (void)sinkBuffer;
}

static void trace_sink_buffer_destroy(TraceSink* sink) {
  TraceSinkBuffer* sinkBuffer = (TraceSinkBuffer*)sink;
  (void)sinkBuffer;
}

TraceSink* trace_sink_buffer(Allocator* alloc) {
  TraceSinkBuffer* sink = alloc_alloc_t(alloc, TraceSinkBuffer);

  *sink = (TraceSinkBuffer){
      .api =
          {
              .eventBegin = trace_sink_buffer_event_begin,
              .eventEnd   = trace_sink_buffer_event_end,
              .destroy    = trace_sink_buffer_destroy,
          },
      .alloc = alloc,
  };

  return (TraceSink*)sink;
}
