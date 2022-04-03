#include "core_alloc.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log.h"
#include "log_sink.h"
#include "ui.h"

/**
 * Sink that will receive logger messages.
 * NOTE: Needs a stable pointer as it will be registered to the logger.
 * NOTE: Sink needs to stay alive as long as the logger still exists or the viewer still exists,
 * to achieve this it has a basic ref-counter.
 */
typedef struct {
  LogSink api;
  i64     refCounter;
} DebugLogSink;

static void debug_log_sink_write(
    LogSink*        sink,
    LogLevel        lvl,
    SourceLoc       srcLoc,
    TimeReal        timestamp,
    String          message,
    const LogParam* params) {
  DebugLogSink* debugSink = (DebugLogSink*)sink;

  (void)debugSink;
  (void)lvl;
  (void)srcLoc;
  (void)timestamp;
  (void)message;
  (void)params;
}

static void debug_log_sink_destroy(LogSink* sink) {
  DebugLogSink* debugSink = (DebugLogSink*)sink;
  if (thread_atomic_sub_i64(&debugSink->refCounter, 1) == 1) {
    alloc_free_t(g_alloc_heap, debugSink);
  }
}

DebugLogSink* debug_log_sink_create() {
  DebugLogSink* sink = alloc_alloc_t(g_alloc_heap, DebugLogSink);
  *sink = (DebugLogSink){.api = {.write = debug_log_sink_write, .destroy = debug_log_sink_destroy}};
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
