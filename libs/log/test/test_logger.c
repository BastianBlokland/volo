#include "check_spec.h"
#include "core_alloc.h"
#include "log.h"
#include "log_sink.h"

typedef struct {
  LogLevel lvl;
  String   message;
  TimeReal timestamp;
} SinkTestMsg;

typedef struct {
  LogSink  api;
  DynArray messages; // SinkTestMsg[]
} SinkTest;

static void log_sink_test_write(
    LogSink*        sink,
    LogLevel        lvl,
    SourceLoc       srcLoc,
    TimeReal        timestamp,
    String          message,
    const LogParam* params) {
  SinkTest* testSink = (SinkTest*)sink;

  (void)sink;
  (void)srcLoc;
  (void)timestamp;
  (void)params;

  *dynarray_push_t(&testSink->messages, SinkTestMsg) = (SinkTestMsg){
      .lvl     = lvl,
      .message = string_dup(g_alloc_heap, message),
  };
}

spec(logger) {

  TimeReal startTime = 0;
  Logger*  logger    = null;
  SinkTest sink      = {0};

  setup() {
    startTime = time_real_clock();
    logger    = log_create(g_alloc_heap);
    sink      = (SinkTest){
        .api      = (LogSink){.write = log_sink_test_write},
        .messages = dynarray_create_t(g_alloc_heap, SinkTestMsg, 8),
    };
    log_add_sink(logger, (LogSink*)&sink);
  }

  it("sends received message to attached sinks") {
    log(logger, LogLevel_Error, "Hello World");

    SinkTestMsg* msg = dynarray_at_t(&sink.messages, 0, SinkTestMsg);
    check_eq_int(msg->lvl, LogLevel_Error);
    check_eq_string(msg->message, string_lit("Hello World"));
  }

  it("supports formatting the message with parameters") {
    log(logger,
        LogLevel_Error,
        "Initializing {} stage {}",
        log_param("name", fmt_text_lit("System9000")),
        log_param("stage", fmt_int(2)));

    SinkTestMsg* msg = dynarray_at_t(&sink.messages, 0, SinkTestMsg);
    check_eq_string(msg->message, string_lit("Initializing System9000 stage 2"));
  }

  it("timestamps all messages") {
    log(logger, LogLevel_Info, "Hello World");

    SinkTestMsg* msg = dynarray_at_t(&sink.messages, 0, SinkTestMsg);
    check(time_real_duration(startTime, msg->timestamp) < time_second);
  }

  teardown() {
    log_destroy(logger);

    dynarray_for_t(&sink.messages, SinkTestMsg, msg, { string_free(g_alloc_heap, msg->message); });
    dynarray_destroy(&sink.messages);
  }
}
