#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "json.h"
#include "trace.h"

static JsonVal test_find_event_by_name(const JsonDoc* jDoc, const JsonVal arr, const String name) {
  json_for_elems(jDoc, arr, evt) {
    const JsonVal evtNameVal = json_field(jDoc, evt, string_lit("name"));
    if (string_eq(json_string(jDoc, evtNameVal), name)) {
      return evt;
    }
  }
  return sentinel_u32;
}

spec(dump_eventtrace) {

  Tracer*    tracer;
  TraceSink* storeSink;
  JsonDoc*   jDoc   = null;
  DynString  buffer = {0};

  setup() {
    tracer = trace_create(g_alloc_heap);
    trace_add_sink(tracer, storeSink = trace_sink_store(g_alloc_heap));

    jDoc   = json_create(g_alloc_heap, 64);
    buffer = dynstring_create(g_alloc_heap, 1024);
  }

  it("can dump events") {
    static const String g_evtName = string_static("testEvt");

    trace_event_begin(tracer, g_evtName, TraceColor_Red);
    trace_event_end(tracer);

    trace_dump_eventtrace(storeSink, &buffer);

    JsonResult result;
    json_read(jDoc, dynstring_view(&buffer), &result);
    check_eq_int(result.type, JsonResultType_Success);

    const JsonVal evtArr = json_field(jDoc, result.val, string_lit("traceEvents"));
    const JsonVal evt    = test_find_event_by_name(jDoc, evtArr, g_evtName);

    check_eq_string(json_string(jDoc, json_field(jDoc, evt, string_lit("cat"))), g_evtName);
    check_eq_string(json_string(jDoc, json_field(jDoc, evt, string_lit("ph"))), string_lit("X"));
  }

  teardown() {
    trace_destroy(tracer);
    dynstring_destroy(&buffer);
    json_destroy(jDoc);
  }
}
