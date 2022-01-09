#include "core_alloc.h"
#include "core_diag.h"
#include "core_path.h"
#include "core_time.h"
#include "ecs_runner.h"
#include "jobs_dot.h"
#include "jobs_graph.h"
#include "jobs_scheduler.h"
#include "log_logger.h"

#include "def_internal.h"
#include "view_internal.h"
#include "world_internal.h"

/**
 * Meta systems:
 * - Flush (Applies entity layout modifications).
 */
#define graph_meta_task_count 1

typedef enum {
  EcsRunnerPrivateFlags_Running = 1 << (EcsRunnerFlags_Count + 0),
} EcsRunnerPrivateFlags;

typedef struct {
  EcsRunner* runner;
} MetaTaskData;

typedef struct {
  EcsWorld*        world;
  EcsSystemId      id;
  EcsSystemRoutine routine;
} SystemTaskData;

struct sEcsRunner {
  EcsWorld*  world;
  JobGraph*  graph;
  u32        flags;
  Allocator* alloc;
};

THREAD_LOCAL bool        g_ecsRunningSystem;
THREAD_LOCAL EcsSystemId g_ecsRunningSystemId = sentinel_u16;

static JobTaskFlags graph_system_task_flags(const EcsSystemDef* systemDef) {
  JobTaskFlags flags = JobTaskFlags_None;
  if (systemDef->flags & EcsSystemFlags_ThreadAffinity) {
    flags |= JobTaskFlags_ThreadAffinity;
  }
  return flags;
}

static void graph_runner_flush_task(void* context) {
  MetaTaskData* data = context;
  ecs_world_flush_internal(data->runner->world);

  data->runner->flags &= ~EcsRunnerPrivateFlags_Running;
  ecs_world_busy_unset(data->runner->world);
}

static void graph_system_task(void* context) {
  SystemTaskData* data = context;

  g_ecsRunningSystem   = true;
  g_ecsRunningSystemId = data->id;

  data->routine(data->world);

  g_ecsRunningSystem   = false;
  g_ecsRunningSystemId = sentinel_u16;
}

static JobTaskId graph_insert_flush(EcsRunner* runner) {
  return jobs_graph_add_task(
      runner->graph,
      string_lit("Flush"),
      graph_runner_flush_task,
      mem_struct(MetaTaskData, .runner = runner),
      JobTaskFlags_None);
}

static JobTaskId
graph_insert_system(EcsRunner* runner, const EcsSystemId systemId, const EcsSystemDef* systemDef) {
  return jobs_graph_add_task(
      runner->graph,
      systemDef->name,
      graph_system_task,
      mem_struct(
          SystemTaskData, .world = runner->world, .id = systemId, .routine = systemDef->routine),
      graph_system_task_flags(systemDef));
};

static bool graph_system_conflict(EcsWorld* world, const EcsSystemDef* a, const EcsSystemDef* b) {
  /**
   * Check if two systems have conflicting views meaning they cannot be run in parallel.
   */
  dynarray_for_t((DynArray*)&a->viewIds, EcsViewId, aViewId) {
    EcsView* aView = ecs_world_view(world, *aViewId);

    dynarray_for_t((DynArray*)&b->viewIds, EcsViewId, bViewId) {
      if (ecs_view_conflict(aView, ecs_world_view(world, *bViewId))) {
        return true;
      }
    }
  }
  return false;
}

static void graph_reduce(const EcsRunner* runner) {
  MAYBE_UNUSED const TimeSteady startTime = time_steady_clock();

  const usize depsRemoved = jobs_graph_reduce_dependencies(runner->graph);

  MAYBE_UNUSED const TimeDuration duration = time_steady_duration(startTime, time_steady_clock());
  log_d(
      "Ecs system-graph reduced",
      log_param("deps-removed", fmt_int(depsRemoved)),
      log_param("duration", fmt_duration(duration)));
}

static void graph_dump_dot(const EcsRunner* runner) {
  const String fileName =
      fmt_write_scratch("{}_ecs_graph.dot", fmt_text(path_stem(g_path_executable)));

  const String path =
      path_build_scratch(path_parent(g_path_executable), string_lit("logs"), fileName);

  const FileResult res = jobs_dot_dump_graph_to_path(path, runner->graph);
  if (res == FileResult_Success) {
    log_i("Ecs system-graph dumped", log_param("path", fmt_path(path)));
  } else {
    log_e("Ecs system-graph dump failed", log_param("error", fmt_text(file_result_str(res))));
  }
}

EcsRunner* ecs_runner_create(Allocator* alloc, EcsWorld* world, const EcsRunnerFlags flags) {
  const EcsDef* def       = ecs_world_def(world);
  const usize   taskCount = def->systems.size + graph_meta_task_count;

  EcsRunner* runner = alloc_alloc_t(alloc, EcsRunner);
  *runner           = (EcsRunner){
      .world = world,
      .graph = jobs_graph_create(alloc, string_lit("ecs_runner"), taskCount),
      .flags = flags,
      .alloc = alloc,
  };

  const JobTaskId flushTask = graph_insert_flush(runner);

  for (EcsSystemId sysId = 0; sysId != def->systems.size; ++sysId) {
    EcsSystemDef*   sys       = dynarray_at_t(&def->systems, sysId, EcsSystemDef);
    const JobTaskId sysTaskId = graph_insert_system(runner, sysId, sys);

    // Insert a flush dependency (so flush only happens when all systems are done).
    jobs_graph_task_depend(runner->graph, sysTaskId, flushTask);

    // Insert required dependencies on the earlier systems.
    for (EcsSystemId otherSysId = 0; otherSysId != sysId; ++otherSysId) {
      EcsSystemDef* otherSys = dynarray_at_t(&def->systems, otherSysId, EcsSystemDef);
      if (graph_system_conflict(world, sys, otherSys)) {
        jobs_graph_task_depend(runner->graph, ecs_runner_graph_task(runner, otherSysId), sysTaskId);
      }
    }
  }

  diag_assert(jobs_graph_task_count(runner->graph) == taskCount);

  log_i(
      "Ecs system-graph created",
      log_param("tasks", fmt_int(taskCount)),
      log_param("span", fmt_int(jobs_graph_task_span(runner->graph))),
      log_param("parallelism", fmt_float(jobs_graph_task_parallelism(runner->graph))));

  graph_reduce(runner);

  if (flags & EcsRunnerFlags_DumpGraphDot) {
    graph_dump_dot(runner);
  }
  return runner;
}

void ecs_runner_destroy(EcsRunner* runner) {
  diag_assert_msg(!ecs_running(runner), "Runner is still running");

  jobs_graph_destroy(runner->graph);
  alloc_free_t(runner->alloc, runner);
}

const JobGraph* ecs_runner_graph(const EcsRunner* runner) { return runner->graph; }

JobTaskId ecs_runner_graph_task(const EcsRunner* runner, const EcsSystemId systemId) {
  (void)runner;
  /**
   * Currently systems are added to the JobGraph linearly right after the meta tasks. So to lookup a
   * task-id we only need to offset.
   */
  return (JobTaskId)(graph_meta_task_count + systemId);
}

bool ecs_running(const EcsRunner* runner) {
  return (runner->flags & EcsRunnerPrivateFlags_Running) != 0;
}

JobId ecs_run_async(EcsRunner* runner) {
  diag_assert_msg(!ecs_running(runner), "Runner is currently already running");

  runner->flags |= EcsRunnerPrivateFlags_Running;
  ecs_world_busy_set(runner->world);
  return jobs_scheduler_run(runner->graph);
}

void ecs_run_sync(EcsRunner* runner) {
  const JobId job = ecs_run_async(runner);
  jobs_scheduler_wait_help(job);
}
