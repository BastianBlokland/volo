#include "check_spec.h"
#include "core_alloc.h"
#include "core_dynarray.h"
#include "trace_sink.h"
#include "trace_tracer.h"

typedef struct {
  String     id;
  String     msg;
  TraceColor color;
} SinkTestEvt;

typedef struct {
  TraceSink api;
  DynArray  events; // SinkTestEvt[]
  u32       endCount;
} SinkTest;

static void trace_sink_test_event_begin(
    TraceSink* sink, const String id, const TraceColor color, const String msg) {
  SinkTest* testSink = (SinkTest*)sink;

  *dynarray_push_t(&testSink->events, SinkTestEvt) = (SinkTestEvt){
      .id    = string_dup(g_allocHeap, id),
      .msg   = string_maybe_dup(g_allocHeap, msg),
      .color = color,
  };
}

static void trace_sink_test_event_end(TraceSink* sink) {
  SinkTest* testSink = (SinkTest*)sink;
  testSink->endCount++;
}

spec(tracer) {
  Tracer*  tracer = null;
  SinkTest sink   = {0};

  setup() {
    tracer = trace_create(g_allocHeap);
    sink   = (SinkTest){
          .api =
            (TraceSink){
                  .eventBegin = trace_sink_test_event_begin,
                  .eventEnd   = trace_sink_test_event_end,
            },
          .events = dynarray_create_t(g_allocHeap, SinkTestEvt, 8),
    };
    trace_add_sink(tracer, (TraceSink*)&sink);
  }

  it("sends events to attached sinks") {
    trace_event_begin(tracer, string_lit("testEvt"), TraceColor_Red);
    trace_event_end(tracer);

    check_eq_int(sink.events.size, 1);
    check_eq_int(sink.endCount, 1);

    SinkTestEvt* evt = dynarray_at_t(&sink.events, 0, SinkTestEvt);
    check_eq_string(evt->id, string_lit("testEvt"));
    check_eq_string(evt->msg, string_empty);
    check_eq_int(evt->color, TraceColor_Red);
  }

  it("supports events with formatted messages") {
    trace_event_begin_msg(
        tracer,
        string_lit("testEvt"),
        TraceColor_Blue,
        string_lit("message {}"),
        fmt_args(fmt_int(42)));
    trace_event_end(tracer);

    check_eq_int(sink.events.size, 1);
    check_eq_int(sink.endCount, 1);

    SinkTestEvt* evt = dynarray_at_t(&sink.events, 0, SinkTestEvt);
    check_eq_string(evt->id, string_lit("testEvt"));
    check_eq_string(evt->msg, string_lit("message 42"));
    check_eq_int(evt->color, TraceColor_Blue);
  }

  teardown() {
    trace_destroy(tracer);

    dynarray_for_t(&sink.events, SinkTestEvt, evt) {
      string_free(g_allocHeap, evt->id);
      string_maybe_free(g_allocHeap, evt->msg);
    }
    dynarray_destroy(&sink.events);
  }
}
