#include "check_spec.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "json_doc.h"
#include "json_read.h"
#include "trace_dump.h"
#include "trace_sink_store.h"
#include "trace_tracer.h"

static JsonVal test_find_event_by_name(const JsonDoc* jDoc, const JsonVal arr, const String name) {
  json_for_elems(jDoc, arr, evt) {
    const JsonVal evtNameVal = json_field_lit(jDoc, evt, "name");
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
    tracer = trace_create(g_allocHeap);
    trace_add_sink(tracer, storeSink = trace_sink_store(g_allocHeap));

    jDoc   = json_create(g_allocHeap, 64);
    buffer = dynstring_create(g_allocHeap, 1024);
  }

  it("can dump events") {
    static const String g_evtName = string_static("testEvt");

    trace_event_begin(tracer, g_evtName, TraceColor_Red);
    trace_event_end(tracer);

    trace_dump_eventtrace(storeSink, &buffer);

    JsonResult result;
    json_read(jDoc, dynstring_view(&buffer), JsonReadFlags_None, &result);
    check_eq_int(result.type, JsonResultType_Success);

    const JsonVal evtArr = json_field_lit(jDoc, result.val, "traceEvents");
    const JsonVal evt    = test_find_event_by_name(jDoc, evtArr, g_evtName);

    check_eq_string(json_string(jDoc, json_field_lit(jDoc, evt, "cat")), g_evtName);
    check_eq_string(json_string(jDoc, json_field_lit(jDoc, evt, "ph")), string_lit("X"));
  }

  teardown() {
    trace_destroy(tracer);
    dynstring_destroy(&buffer);
    json_destroy(jDoc);
  }
}
