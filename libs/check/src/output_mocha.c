#include "core_diag.h"
#include "core_format.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_time.h"

#include "json.h"

#include "output_mocha.h"

typedef struct {
  CheckOutput api;
  Allocator*  alloc;
  ThreadMutex mutex;
  JsonDoc*    doc;
  JsonVal     rootObj;
  JsonVal     statsObj;
  JsonVal     passesArr;
  JsonVal     failuresArr;
  JsonVal     pendingArr;
  File*       file;
} CheckOutputMocha;

static void mocha_write_json(JsonDoc* doc, JsonVal rootObj, File* file) {
  DynString dynString = dynstring_create(g_alloc_heap, 64 * usize_kibibyte);

  json_write(&dynString, doc, rootObj, &json_write_opts());

  const FileResult writeRes = file_write_sync(file, dynstring_view(&dynString));
  if (writeRes != FileResult_Success) {
    diag_crash_msg(
        "Failed to write mocha test-results to file: {}", fmt_text(file_result_str(writeRes)));
  }

  dynstring_destroy(&dynString);
}

static JsonVal mocha_add_test_obj(JsonDoc* doc, const CheckSpec* spec, const CheckTest* test) {
  const JsonVal testObj = json_add_object(doc);
  json_add_field_str(doc, testObj, string_lit("title"), json_add_string(doc, test->description));

  json_add_field_str(
      doc,
      testObj,
      string_lit("fullTitle"),
      json_add_string(
          doc, fmt_write_scratch("{} {}", fmt_text(spec->def->name), fmt_text(test->description))));

  json_add_field_str(doc, testObj, string_lit("file"), json_add_string(doc, test->source.file));

  return testObj;
}

static void output_run_started(CheckOutput* out) {
  CheckOutputMocha* mochaOut = (CheckOutputMocha*)out;
  JsonDoc*          doc      = mochaOut->doc;

  const TimeReal startTime = time_real_clock();
  json_add_field_str(
      doc,
      mochaOut->statsObj,
      string_lit("start"),
      json_add_string(doc, format_write_arg_scratch(&fmt_time(startTime))));
}

static void output_tests_discovered(
    CheckOutput* out, const usize specCount, const usize testCount, const TimeDuration dur) {
  CheckOutputMocha* mochaOut = (CheckOutputMocha*)out;
  JsonDoc*          doc      = mochaOut->doc;

  (void)dur;

  json_add_field_str(
      doc, mochaOut->statsObj, string_lit("suites"), json_add_number(doc, specCount));

  json_add_field_str(doc, mochaOut->statsObj, string_lit("tests"), json_add_number(doc, testCount));
}

static void output_test_skipped(CheckOutput* out, const CheckSpec* spec, const CheckTest* test) {
  CheckOutputMocha* mochaOut = (CheckOutputMocha*)out;
  JsonDoc*          doc      = mochaOut->doc;

  const JsonVal testObj = mocha_add_test_obj(doc, spec, test);
  json_add_elem(doc, mochaOut->pendingArr, testObj);

  // Note: Add an empty 'err' object as a few consumers depend on this existing.
  const JsonVal errObj = json_add_object(doc);
  json_add_field_str(doc, testObj, string_lit("err"), errObj);
}

static void output_test_finished(
    CheckOutput*          out,
    const CheckSpec*      spec,
    const CheckTest*      test,
    const CheckResultType type,
    CheckResult*          result) {
  CheckOutputMocha* mochaOut = (CheckOutputMocha*)out;
  JsonDoc*          doc      = mochaOut->doc;

  thread_mutex_lock(mochaOut->mutex);

  const JsonVal testObj = mocha_add_test_obj(mochaOut->doc, spec, test);

  json_add_field_str(
      doc,
      testObj,
      string_lit("duration"),
      json_add_number(doc, result->duration / (f64)time_millisecond));

  const JsonVal errObj = json_add_object(doc);
  json_add_field_str(doc, testObj, string_lit("err"), errObj);

  if (result->errors.size) {
    // Note: Unfortunately Mocha's format only supports one error per test, so we take the first.
    const CheckError* err = dynarray_at_t(&result->errors, 0, CheckError);
    json_add_field_str(doc, errObj, string_lit("message"), json_add_string(doc, err->msg));

    /**
     * Emulate the nodejs stack-trace format that consumers of the Mocha json format expect.
     * Example nodejs stack-trace:
     * ```
     * This program demonstrates stack trace in Node.js
     * Error
     *   at Object. (/home/cg/root/2523129/main.js:20:11)
     *   at Module._compile (module.js:570:32)
     * ```
     *
     * We don't capture stack-traces for errors at the moment, but we do know the top-most
     * stack-frame which triggered the error.
     */
    json_add_field_str(
        doc,
        errObj,
        string_lit("stack"),
        json_add_string(
            doc,
            fmt_write_scratch(
                "{}\nat {} ({}:{}:0)",
                fmt_text(err->msg),
                fmt_text(spec->def->name),
                fmt_path(err->source.file),
                fmt_int(err->source.line))));
  }

  switch (type) {
  case CheckResultType_Pass:
    json_add_elem(mochaOut->doc, mochaOut->passesArr, testObj);
    break;
  case CheckResultType_Fail:
    json_add_elem(mochaOut->doc, mochaOut->failuresArr, testObj);
    break;
  }

  thread_mutex_unlock(mochaOut->mutex);
}

static void output_run_finished(
    CheckOutput*          out,
    const CheckResultType type,
    const TimeDuration    dur,
    const usize           numPassed,
    const usize           numFailed,
    const usize           numSkipped) {
  CheckOutputMocha* mochaOut = (CheckOutputMocha*)out;
  JsonDoc*          doc      = mochaOut->doc;

  (void)type;

  json_add_field_str(
      doc, mochaOut->statsObj, string_lit("passes"), json_add_number(doc, numPassed));

  json_add_field_str(
      doc, mochaOut->statsObj, string_lit("failures"), json_add_number(doc, numFailed));

  json_add_field_str(
      doc, mochaOut->statsObj, string_lit("pending"), json_add_number(doc, numSkipped));

  const TimeReal endTime = time_real_clock();
  json_add_field_str(
      doc,
      mochaOut->statsObj,
      string_lit("end"),
      json_add_string(doc, format_write_arg_scratch(&fmt_time(endTime))));

  json_add_field_str(
      doc,
      mochaOut->statsObj,
      string_lit("duration"),
      json_add_number(doc, dur / (f64)time_millisecond));
}

static void output_destroy(CheckOutput* out) {
  CheckOutputMocha* mochaOut = (CheckOutputMocha*)out;

  mocha_write_json(mochaOut->doc, mochaOut->rootObj, mochaOut->file);

  thread_mutex_destroy(mochaOut->mutex);
  file_destroy(mochaOut->file);
  json_destroy(mochaOut->doc);

  alloc_free_t(mochaOut->alloc, mochaOut);
}

CheckOutput* check_output_mocha(Allocator* alloc, File* file) {
  JsonDoc*      doc         = json_create(alloc, 512);
  const JsonVal rootObj     = json_add_object(doc);
  const JsonVal statsObj    = json_add_object(doc);
  const JsonVal passesArr   = json_add_array(doc);
  const JsonVal failuresArr = json_add_array(doc);
  const JsonVal pendingArr  = json_add_array(doc);

  json_add_field_str(doc, rootObj, string_lit("stats"), statsObj);
  json_add_field_str(doc, rootObj, string_lit("passes"), passesArr);
  json_add_field_str(doc, rootObj, string_lit("failures"), failuresArr);
  json_add_field_str(doc, rootObj, string_lit("pending"), pendingArr);

  CheckOutputMocha* mochaOut = alloc_alloc_t(alloc, CheckOutputMocha);
  *mochaOut                  = (CheckOutputMocha){
      .api =
          {
              .runStarted      = output_run_started,
              .testsDiscovered = output_tests_discovered,
              .testSkipped     = output_test_skipped,
              .testFinished    = output_test_finished,
              .runFinished     = output_run_finished,
              .destroy         = output_destroy,
          },
      .alloc       = alloc,
      .mutex       = thread_mutex_create(alloc),
      .doc         = doc,
      .rootObj     = rootObj,
      .statsObj    = statsObj,
      .passesArr   = passesArr,
      .failuresArr = failuresArr,
      .pendingArr  = pendingArr,
      .file        = file,
  };
  return (CheckOutput*)mochaOut;
}

CheckOutput* check_output_mocha_to_path(Allocator* alloc, String path) {
  File*      file;
  FileResult res;
  if ((res = file_create_dir_sync(path_parent(path))) != FileResult_Success) {
    diag_crash_msg("Failed to create parent directory: {}", fmt_text(file_result_str(res)));
  }
  if ((res = file_create(alloc, path, FileMode_Create, FileAccess_Write, &file)) !=
      FileResult_Success) {
    diag_crash_msg("Failed to create mocha test-result file: {}", fmt_text(file_result_str(res)));
  }
  return check_output_mocha(alloc, file);
}

CheckOutput* check_output_mocha_default(Allocator* alloc) {
  const String resultPath = path_build_scratch(
      path_parent(g_path_executable),
      string_lit("logs"),
      path_name_timestamp_scratch(path_stem(g_path_executable), string_lit("mocha")));
  return check_output_mocha_to_path(alloc, resultPath);
}
