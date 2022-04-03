#include "core_alloc.h"
#include "core_math.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log.h"
#include "log_logger.h"
#include "log_sink.h"
#include "ui.h"

//#define log_viewer_mask (LogMask_Warn | LogMask_Error)
#define log_viewer_mask (LogMask_All)
#define log_viewer_max_message_size 128

typedef struct {
  TimeReal timestamp;
  LogLevel lvl;
  u32      length;
  u8       data[log_viewer_max_message_size];
} DebugLogMessage;

/**
 * Sink that will receive logger messages.
 * NOTE: Needs a stable pointer as it will be registered to the logger.
 * NOTE: Sink needs to stay alive as long as the logger still exists or the viewer still exists,
 * to achieve this it has a basic ref-counter.
 */
typedef struct {
  LogSink        api;
  i64            refCounter;
  ThreadSpinLock messagesLock;
  DynArray       messages; // DebugLogMessage[].
} DebugLogSink;

static void debug_log_sink_write(
    LogSink*        sink,
    LogLevel        lvl,
    SourceLoc       srcLoc,
    TimeReal        timestamp,
    String          message,
    const LogParam* params) {
  DebugLogSink* debugSink = (DebugLogSink*)sink;
  if ((log_viewer_mask & (1 << lvl)) == 0) {
    return;
  }
  thread_spinlock_lock(&debugSink->messagesLock);
  {
    (void)srcLoc;
    (void)params;
    DebugLogMessage* msg = dynarray_push_t(&debugSink->messages, DebugLogMessage);
    msg->lvl             = lvl;
    msg->timestamp       = timestamp;
    msg->length          = math_min((u32)message.size, log_viewer_max_message_size);
    mem_cpy(mem_create(msg->data, msg->length), string_consume(message, msg->length));
  }
  thread_spinlock_unlock(&debugSink->messagesLock);
}

static void debug_log_sink_destroy(LogSink* sink) {
  DebugLogSink* debugSink = (DebugLogSink*)sink;
  if (thread_atomic_sub_i64(&debugSink->refCounter, 1) == 1) {
    dynarray_destroy(&debugSink->messages);
    alloc_free_t(g_alloc_heap, debugSink);
  }
}

DebugLogSink* debug_log_sink_create() {
  DebugLogSink* sink = alloc_alloc_t(g_alloc_heap, DebugLogSink);

  *sink = (DebugLogSink){
      .api      = {.write = debug_log_sink_write, .destroy = debug_log_sink_destroy},
      .messages = dynarray_create_t(g_alloc_heap, DebugLogMessage, 64),
  };
  return sink;
}

ecs_comp_define(DebugLogViewerComp) { DebugLogSink* sink; };

static void ecs_destruct_log_viewer(void* data) {
  DebugLogViewerComp* comp = data;
  debug_log_sink_destroy((LogSink*)comp->sink);
}

ecs_view_define(DebugLogGlobalView) { ecs_access_write(DebugLogViewerComp); }

static DebugLogViewerComp* debug_log_viewer_global(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, DebugLogGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, DebugLogViewerComp) : null;
}

static DebugLogViewerComp*
debug_log_viewer_create(EcsWorld* world, const EcsEntityId entity, Logger* logger) {
  DebugLogSink* sink = debug_log_sink_create();
  thread_atomic_store_i64(&sink->refCounter, 2); // Referenced by the logger and the viewer.
  log_add_sink(logger, (LogSink*)sink);
  return ecs_world_add_t(world, entity, DebugLogViewerComp, .sink = sink);
}

ecs_system_define(DebugLogUpdateSys) {
  DebugLogViewerComp* viewerGlobal = debug_log_viewer_global(world);
  if (!viewerGlobal) {
    viewerGlobal = debug_log_viewer_create(world, ecs_world_global(world), g_logger);
  }
}

ecs_module_init(debug_log_viewer_module) {
  ecs_register_comp(DebugLogViewerComp, .destructor = ecs_destruct_log_viewer);

  ecs_register_view(DebugLogGlobalView);

  ecs_register_system(DebugLogUpdateSys, ecs_view_id(DebugLogGlobalView));
}
