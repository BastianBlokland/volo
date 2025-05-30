#include "check_spec.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "json_doc.h"
#include "json_read.h"
#include "log_logger.h"
#include "log_sink_json.h"

spec(sink_json) {

  Logger*   logger  = null;
  File*     tmpFile = null;
  JsonDoc*  jsonDoc = null;
  DynString buffer  = {0};

  setup() {
    logger = log_create(g_allocHeap);
    file_temp(g_allocHeap, &tmpFile);

    const LogMask mask = LogMask_Info | LogMask_Warn;
    log_add_sink(logger, log_sink_json(g_allocHeap, tmpFile, mask, LogSinkJsonFlags_DestroyFile));

    jsonDoc = json_create(g_allocHeap, 64);
    buffer  = dynstring_create(g_allocHeap, 1024);
  }

  it("supports plain log messages without parameters") {
    log(logger, LogLevel_Info, "Hello World");

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    JsonResult result;
    json_read(jsonDoc, dynstring_view(&buffer), JsonReadFlags_None, &result);
    check_eq_int(result.type, JsonResultType_Success);

    check_eq_string(
        json_string(jsonDoc, json_field_lit(jsonDoc, result.val, "message")),
        string_lit("Hello World"));
    check_eq_string(
        json_string(jsonDoc, json_field_lit(jsonDoc, result.val, "level")), string_lit("inf"));
    check_eq_float(json_number(jsonDoc, json_field_lit(jsonDoc, result.val, "line")), 29, 1e-6);
  }

  it("supports log messages with parameters") {
    log(logger, LogLevel_Info, "Hello World", log_param("param", fmt_int(42)));

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    JsonResult result;
    json_read(jsonDoc, dynstring_view(&buffer), JsonReadFlags_None, &result);
    check_eq_int(result.type, JsonResultType_Success);

    JsonVal extraObj = json_field_lit(jsonDoc, result.val, "extra");
    check_eq_float(json_number(jsonDoc, json_field_lit(jsonDoc, extraObj, "param")), 42, 1e-6);
  }

  it("supports list parameters") {
    log(logger,
        LogLevel_Info,
        "Hello World",
        log_param("param", fmt_list_lit(fmt_int(1), fmt_int(2), fmt_int(3))));

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    JsonResult result;
    json_read(jsonDoc, dynstring_view(&buffer), JsonReadFlags_None, &result);
    check_eq_int(result.type, JsonResultType_Success);

    JsonVal extraObj = json_field_lit(jsonDoc, result.val, "extra");
    JsonVal paramArr = json_field_lit(jsonDoc, extraObj, "param");
    check_eq_int(json_elem_count(jsonDoc, paramArr), 3);
    check_eq_float(json_number(jsonDoc, json_elem(jsonDoc, paramArr, 0)), 1, 1e-6);
    check_eq_float(json_number(jsonDoc, json_elem(jsonDoc, paramArr, 1)), 2, 1e-6);
    check_eq_float(json_number(jsonDoc, json_elem(jsonDoc, paramArr, 2)), 3, 1e-6);
  }

  it("supports formatted messages") {
    log(logger, LogLevel_Info, "Hello{}World", log_param("param", fmt_int(42)));

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    JsonResult result;
    json_read(jsonDoc, dynstring_view(&buffer), JsonReadFlags_None, &result);
    check_eq_int(result.type, JsonResultType_Success);

    check_eq_string(
        json_string(jsonDoc, json_field_lit(jsonDoc, result.val, "message")),
        string_lit("Hello42World"));
  }

  it("ignores messages which do not match the mask") {
    log(logger, LogLevel_Error, "Hello World");

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    check_eq_string(dynstring_view(&buffer), string_empty);
  }

  teardown() {
    dynstring_destroy(&buffer);
    log_destroy(logger);
    json_destroy(jsonDoc);
  }
}
