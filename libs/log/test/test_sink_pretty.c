#include "check_spec.h"
#include "core_alloc.h"
#include "core_file.h"
#include "log.h"

spec(sink_pretty) {

  Logger*   logger  = null;
  File*     tmpFile = null;
  DynString buffer  = {0};

  setup() {
    logger = log_create(g_alloc_heap);
    file_temp(g_alloc_heap, &tmpFile);

    const LogMask mask = LogMask_Info | LogMask_Warn;
    log_add_sink(
        logger, log_sink_pretty(g_alloc_heap, tmpFile, mask, LogSinkPrettyFlags_DestroyFile));

    buffer = dynstring_create(g_alloc_heap, 1024);
  }

  it("supports plain log messages without parameters") {
    log(logger, LogLevel_Info, "Hello World");

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    check(string_match_glob(
        dynstring_view(&buffer), string_lit("* [inf] Hello World\n"), StringMatchFlags_None));
  }

  it("supports log messages with parameters") {
    log(logger, LogLevel_Info, "Hello World", log_param("param", fmt_int(42)));

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    check(string_match_glob(
        dynstring_view(&buffer),
        string_lit("* [inf] Hello World\n"
                   "  param: 42\n"),
        StringMatchFlags_None));
  }

  it("aligns parameter values") {
    log(logger,
        LogLevel_Info,
        "Hello World",
        log_param("short", fmt_int(42)),
        log_param("very-very-long", fmt_text_lit("Hi!")),
        log_param("shorter", fmt_duration(time_second)));

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    check(string_match_glob(
        dynstring_view(&buffer),
        string_lit("* [inf] Hello World\n"
                   "  short:          42\n"
                   "  very-very-long: Hi!\n"
                   "  shorter:        1s\n"),
        StringMatchFlags_None));
  }

  it("supports list parameters") {
    log(logger,
        LogLevel_Info,
        "Hello World",
        log_param("param", fmt_list_lit(fmt_int(1), fmt_int(2), fmt_int(3))));

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    check(string_match_glob(
        dynstring_view(&buffer),
        string_lit("* [inf] Hello World\n"
                   "  param: 1, 2, 3\n"),
        StringMatchFlags_None));
  }

  it("supports formatted messages") {
    log(logger, LogLevel_Info, "Hello{}World", log_param("param", fmt_int(42)));

    file_seek_sync(tmpFile, 0);
    file_read_to_end_sync(tmpFile, &buffer);

    check(string_match_glob(
        dynstring_view(&buffer),
        string_lit("* [inf] Hello42World\n"
                   "  param: 42\n"),
        StringMatchFlags_None));
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
  }
}
