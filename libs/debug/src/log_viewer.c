#include "core_alloc.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs_world.h"
#include "log.h"
#include "log_logger.h"
#include "log_sink.h"
#include "ui.h"

//#define log_tracker_mask (LogMask_Warn | LogMask_Error)
#define log_tracker_mask (LogMask_All)
#define log_tracker_max_message_size 128
#define log_tracker_max_age time_seconds(2)

typedef struct {
  TimeReal timestamp;
  LogLevel lvl;
  u32      length;
  u8       data[log_tracker_max_message_size];
} DebugLogMessage;

/**
 * Sink that will receive logger messages.
 * NOTE: Needs a stable pointer as it will be registered to the logger.
 * NOTE: Sink needs to stay alive as long as the logger still exists or the tracker still exists,
 * to achieve this it has a basic ref-counter.
 */
typedef struct {
  LogSink        api;
  i64            refCounter;
  ThreadSpinLock messagesLock;
  DynArray       messages; // DebugLogMessage[], sorted on timestamp.
} DebugLogSink;

static void debug_log_sink_write(
    LogSink*        sink,
    LogLevel        lvl,
    SourceLoc       srcLoc,
    TimeReal        timestamp,
    String          message,
    const LogParam* params) {
  DebugLogSink* debugSink = (DebugLogSink*)sink;
  if ((log_tracker_mask & (1 << lvl)) == 0) {
    return;
  }
  thread_spinlock_lock(&debugSink->messagesLock);
  {
    (void)srcLoc;
    (void)params;
    DebugLogMessage* msg = dynarray_push_t(&debugSink->messages, DebugLogMessage);
    msg->lvl             = lvl;
    msg->timestamp       = timestamp;
    msg->length          = math_min((u32)message.size, log_tracker_max_message_size);
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

static void debug_log_sink_prune_older(DebugLogSink* debugSink, const TimeReal timestamp) {
  thread_spinlock_lock(&debugSink->messagesLock);
  {
    usize keepIndex = 0;
    for (; keepIndex != debugSink->messages.size; ++keepIndex) {
      if (dynarray_at_t(&debugSink->messages, keepIndex, DebugLogMessage)->timestamp >= timestamp) {
        break;
      }
    }
    dynarray_remove(&debugSink->messages, 0, keepIndex);
  }
  thread_spinlock_unlock(&debugSink->messagesLock);
}

DebugLogSink* debug_log_sink_create() {
  DebugLogSink* sink = alloc_alloc_t(g_alloc_heap, DebugLogSink);

  *sink = (DebugLogSink){
      .api      = {.write = debug_log_sink_write, .destroy = debug_log_sink_destroy},
      .messages = dynarray_create_t(g_alloc_heap, DebugLogMessage, 64),
  };
  return sink;
}

ecs_comp_define(DebugLogTrackerComp) { DebugLogSink* sink; };

static void ecs_destruct_log_tracker(void* data) {
  DebugLogTrackerComp* comp = data;
  debug_log_sink_destroy((LogSink*)comp->sink);
}

ecs_view_define(DebugLogGlobalView) { ecs_access_write(DebugLogTrackerComp); }

static DebugLogTrackerComp* debug_log_tracker_global(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, DebugLogGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, DebugLogTrackerComp) : null;
}

static DebugLogTrackerComp*
debug_log_tracker_create(EcsWorld* world, const EcsEntityId entity, Logger* logger) {
  DebugLogSink* sink = debug_log_sink_create();
  thread_atomic_store_i64(&sink->refCounter, 2); // Referenced by the logger and the viewer.
  log_add_sink(logger, (LogSink*)sink);
  return ecs_world_add_t(world, entity, DebugLogTrackerComp, .sink = sink);
}

ecs_system_define(DebugLogUpdateSys) {
  DebugLogTrackerComp* viewerGlobal = debug_log_tracker_global(world);
  if (!viewerGlobal) {
    viewerGlobal = debug_log_tracker_create(world, ecs_world_global(world), g_logger);
  }
  const TimeReal oldestToKeep = time_real_offset(time_real_clock(), -log_tracker_max_age);
  debug_log_sink_prune_older(viewerGlobal->sink, oldestToKeep);
}

ecs_module_init(debug_log_viewer_module) {
  ecs_register_comp(DebugLogTrackerComp, .destructor = ecs_destruct_log_tracker);

  ecs_register_view(DebugLogGlobalView);

  ecs_register_system(DebugLogUpdateSys, ecs_view_id(DebugLogGlobalView));
}
