#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "trace_sink_store.h"
#include "trace_tracer.h"

#define test_visitor_max_entries 8

typedef struct {
  ThreadId        threadId;
  String          threadName;
  String          evtId;
  TraceStoreEvent evt;
} TestVisitorEntry;

typedef struct {
  TestVisitorEntry entries[test_visitor_max_entries];
  u32              entryCount;
} TestVisitorCtx;

static void trace_sink_store_test_visitor(
    const TraceSink*       sink,
    void*                  userCtx,
    const u32              bufferIdx,
    const ThreadId         threadId,
    const String           threadName,
    const TraceStoreEvent* evt) {
  TestVisitorCtx* ctx = userCtx;
  diag_assert(ctx->entryCount != test_visitor_max_entries);

  (void)bufferIdx;

  ctx->entries[ctx->entryCount++] = (TestVisitorEntry){
      .threadId   = threadId,
      .threadName = threadName,
      .evtId      = trace_sink_store_id(sink, evt->id),
      .evt        = *evt,
  };
}

spec(sink_store) {

  Tracer*    tracer;
  TraceSink* storeSink;

  setup() {
    tracer = trace_create(g_allocHeap);
    trace_add_sink(tracer, storeSink = trace_sink_store(g_allocHeap));
  }

  it("records events") {
    trace_event_begin(tracer, string_lit("testEvt"), TraceColor_Red);
    trace_event_end(tracer);

    TestVisitorCtx ctx = {0};
    trace_sink_store_visit(storeSink, trace_sink_store_test_visitor, &ctx);

    check_eq_int(ctx.entryCount, 1);
    TestVisitorEntry* entry = &ctx.entries[0];
    check_eq_int(entry->threadId, g_threadTid);
    check_eq_string(entry->threadName, g_threadName);
    check(entry->evt.timeDur != 0);
    check_eq_int(entry->evt.color, TraceColor_Red);
    check_eq_string(entry->evtId, string_lit("testEvt"));
  }

  it("records formatted events") {
    trace_event_begin_msg(
        tracer,
        string_lit("testEvt"),
        TraceColor_Blue,
        string_lit("message {}"),
        fmt_args(fmt_int(42)));
    trace_event_end(tracer);

    TestVisitorCtx ctx = {0};
    trace_sink_store_visit(storeSink, trace_sink_store_test_visitor, &ctx);

    check_eq_int(ctx.entryCount, 1);
    TestVisitorEntry* entry = &ctx.entries[0];
    check_eq_int(entry->threadId, g_threadTid);
    check_eq_string(entry->threadName, g_threadName);
    check(entry->evt.timeDur != 0);
    check_eq_int(entry->evt.color, TraceColor_Blue);
    check_eq_string(entry->evtId, string_lit("testEvt"));
    check_eq_string(mem_create(entry->evt.msgData, entry->evt.msgLength), string_lit("message 42"));
  }

  it("can find a registered store-sink") {
    TraceSink* foundSink = trace_sink_store_find(tracer);
    check_eq_int((uptr)foundSink, (uptr)storeSink);
  }

  teardown() { trace_destroy(tracer); }
}
