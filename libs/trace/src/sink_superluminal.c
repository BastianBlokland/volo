#include "core_alloc.h"
#include "trace_sink.h"
#include "trace_sink_superluminal.h"

#include "event_internal.h"

typedef struct {
  TraceSink  api;
  Allocator* alloc;
} TraceSinkSl;

static void trace_sink_sl_event_begin(
    TraceSink* sink, const String id, const TraceColor color, const String msg) {
  TraceSinkSl* slSink = (TraceSinkSl*)sink;
  (void)slSink;
  (void)id;
  (void)color;
  (void)msg;
}

static void trace_sink_sl_event_end(TraceSink* sink) {
  TraceSinkSl* slSink = (TraceSinkSl*)sink;
  (void)slSink;
}

static void trace_sink_sl_destroy(TraceSink* sink) {
  TraceSinkSl* slSink = (TraceSinkSl*)sink;
  alloc_free_t(slSink->alloc, slSink);
}

TraceSink* trace_sink_superluminal(Allocator* alloc) {
  TraceSinkSl* sink = alloc_alloc_t(alloc, TraceSinkSl);
  *sink             = (TraceSinkSl){
      .api =
          {
              .eventBegin = trace_sink_sl_event_begin,
              .eventEnd   = trace_sink_sl_event_end,
              .destroy    = trace_sink_sl_destroy,
          },
      .alloc = alloc,
  };
  return (TraceSink*)sink;
}
