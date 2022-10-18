#include "core_alloc.h"
#include "core_diag.h"
#include "core_path.h"
#include "core_rng.h"
#include "core_shuffle.h"
#include "core_sort.h"
#include "core_time.h"
#include "ecs_runner.h"
#include "jobs_dot.h"
#include "jobs_executor.h"
#include "jobs_graph.h"
#include "jobs_scheduler.h"
#include "log_logger.h"

#include "def_internal.h"
#include "view_internal.h"
#include "world_internal.h"

/**
 * Meta systems:
 * - Flush (applies entity layout modifications).
 */
#define graph_meta_task_count 1

/**
 * Amount of iterations to use to find the 'optimal' system order (with shortest span).
 */
#define graph_optimization_itrs 100

typedef enum {
  EcsRunnerPrivateFlags_Running = 1 << (EcsRunnerFlags_Count + 0),
} EcsRunnerPrivateFlags;

typedef struct {
  EcsRunner* runner;
} MetaTaskData;

typedef struct {
  const EcsRunner* runner;
  EcsWorld*        world;
  EcsSystemId      id;
  EcsSystemRoutine routine;
} SystemTaskData;

typedef struct {
  EcsSystemId   id;
  i32           order;
  EcsSystemDef* def;
} RunnerSystemEntry;

struct sEcsRunner {
  EcsWorld*   world;
  JobGraph*   graph;
  u32         flags;
  EcsTaskSet* systemTaskSets;
  Allocator*  alloc;
  Mem         jobMem;
};

THREAD_LOCAL bool             g_ecsRunningSystem;
THREAD_LOCAL EcsSystemId      g_ecsRunningSystemId = sentinel_u16;
THREAD_LOCAL const EcsRunner* g_ecsRunningRunner;

static i8 compare_system_entry(const void* a, const void* b) {
  return compare_i32(
      field_ptr(a, RunnerSystemEntry, order), field_ptr(b, RunnerSystemEntry, order));
}

static JobTaskFlags graph_system_task_flags(const EcsSystemDef* systemDef) {
  JobTaskFlags flags = JobTaskFlags_None;
  if (systemDef->flags & EcsSystemFlags_ThreadAffinity) {
    flags |= JobTaskFlags_ThreadAffinity;
  }
  return flags;
}

static void graph_runner_flush_task(void* context) {
  const MetaTaskData* data = context;
  ecs_world_flush_internal(data->runner->world);

  data->runner->flags &= ~EcsRunnerPrivateFlags_Running;
  ecs_world_busy_unset(data->runner->world);
}

static void graph_system_task(void* context) {
  const SystemTaskData* data = context;

  const TimeSteady startTime = time_steady_clock();

  g_ecsRunningSystem   = true;
  g_ecsRunningSystemId = data->id;
  g_ecsRunningRunner   = data->runner;

  data->routine(data->world);

  g_ecsRunningSystem   = false;
  g_ecsRunningSystemId = sentinel_u16;
  g_ecsRunningRunner   = null;

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  ecs_world_stats_sys_add(data->world, data->id, dur);
}

static JobTaskId graph_insert_flush(EcsRunner* runner) {
  /**
   * Insert a task to flush the world (applies entity layout modifications).
   *
   * NOTE: Register the job with 'ThreadAffinity' to handle component destructors that need to be
   * ran on the same thread as its systems (because they need to cleanup thread-local data).
   * This is unfortunately hard to avoid with some of the win32 apis that use thread-local queues.
   */
  return jobs_graph_add_task(
      runner->graph,
      string_lit("Flush"),
      graph_runner_flush_task,
      mem_struct(MetaTaskData, .runner = runner),
      JobTaskFlags_ThreadAffinity);
}

static EcsTaskSet
graph_insert_system(EcsRunner* runner, const EcsSystemId systemId, const EcsSystemDef* systemDef) {

  const JobTaskId taskId = jobs_graph_add_task(
      runner->graph,
      systemDef->name,
      graph_system_task,
      mem_struct(
          SystemTaskData,
          .runner  = runner,
          .world   = runner->world,
          .id      = systemId,
          .routine = systemDef->routine),
      graph_system_task_flags(systemDef));

  return (EcsTaskSet){.begin = taskId, .end = taskId + 1};
}

static bool graph_system_conflict(EcsWorld* world, const EcsSystemDef* a, const EcsSystemDef* b) {
  if ((a->flags & EcsSystemFlags_Exclusive) || (b->flags & EcsSystemFlags_Exclusive)) {
    return true; // Exclusive systems conflict with any other system.
  }

  /**
   * Check if two systems have conflicting views meaning they cannot be run in parallel.
   */
  const EcsView* viewStorage = ecs_world_view_storage_internal(world);
  dynarray_for_t((DynArray*)&a->viewIds, EcsViewId, aViewId) {
    const EcsView* aView = &viewStorage[*aViewId];

    dynarray_for_t((DynArray*)&b->viewIds, EcsViewId, bViewId) {
      const EcsView* bView = &viewStorage[*bViewId];
      if (ecs_view_conflict(aView, bView)) {
        return true;
      }
    }
  }
  return false;
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

static void runner_collect_systems(EcsRunner* runner, RunnerSystemEntry* output) {
  const EcsDef* def = ecs_world_def(runner->world);
  for (EcsSystemId sysId = 0; sysId != def->systems.size; ++sysId) {
    EcsSystemDef* sysDef = dynarray_at_t(&def->systems, sysId, EcsSystemDef);

    output[sysId] = (RunnerSystemEntry){
        .id    = sysId,
        .order = sysDef->order,
        .def   = sysDef,
    };
  }
}

static void runner_populate_graph(EcsRunner* runner, RunnerSystemEntry* systems) {
  const EcsDef*   def         = ecs_world_def(runner->world);
  const usize     systemCount = def->systems.size;
  const JobTaskId flushTask   = graph_insert_flush(runner);

  for (RunnerSystemEntry* entry = systems; entry != systems + systemCount; ++entry) {
    const EcsTaskSet sysTaskSet       = graph_insert_system(runner, entry->id, entry->def);
    runner->systemTaskSets[entry->id] = sysTaskSet;

    // Insert a flush dependency (so flush only happens when all systems are done).
    jobs_graph_task_depend(runner->graph, sysTaskSet.begin, flushTask);

    // Insert required dependencies on the earlier systems.
    for (RunnerSystemEntry* earlierEntry = systems; earlierEntry != entry; ++earlierEntry) {
      if (graph_system_conflict(runner->world, entry->def, earlierEntry->def)) {
        jobs_graph_task_depend(
            runner->graph, ecs_runner_task_set(runner, earlierEntry->id).begin, sysTaskSet.begin);
      }
    }
  }
}

static void runner_sys_cpy(RunnerSystemEntry* dst, RunnerSystemEntry* src, const usize count) {
  const usize memSize = sizeof(RunnerSystemEntry) * count;
  mem_cpy(mem_create(dst, memSize), mem_create(src, memSize));
}

static void runner_compute_graph(EcsRunner* runner, const u32 systemCount) {
  const u32          taskCount   = systemCount + graph_meta_task_count;
  RunnerSystemEntry* systems     = alloc_array_t(runner->alloc, RunnerSystemEntry, systemCount * 2);
  RunnerSystemEntry* bestSystems = systems + systemCount;
  u32                bestSpan    = u32_max; // Lower is better.

  const TimeSteady startTime = time_steady_clock();
  log_d("Ecs computing system-graph", log_param("iterations", fmt_int(graph_optimization_itrs)));

  /**
   * Finding an 'optimal' system order (with shortest span) by brute force creating a random system
   * orders and computing their span.
   * TODO: Think of a clever analytical solution :-)
   */
  runner_collect_systems(runner, systems);
  for (u32 i = 0; i != graph_optimization_itrs; ++i) {
    // Compute a new system order by shuffling and then sorting to respect the constraints.
    shuffle_fisheryates_t(g_rng, systems, systems + systemCount, RunnerSystemEntry);
    sort_bubblesort_t(systems, systems + systemCount, RunnerSystemEntry, compare_system_entry);

    // Fill the graph with tasks for each system.
    jobs_graph_clear(runner->graph);
    runner_populate_graph(runner, systems);

    // Check if the new order is better then our previous best.
    const u32 span = jobs_graph_task_span(runner->graph);
    if (span < bestSpan) {
      runner_sys_cpy(bestSystems, systems, systemCount);
      bestSpan = span;
    }
  }

  // Fill the graph one more time with the best system order we found.
  jobs_graph_clear(runner->graph);
  runner_populate_graph(runner, bestSystems);

  // Remove unecessary dependencies in the graph.
  jobs_graph_reduce_dependencies(runner->graph);

  const TimeDuration duration = time_steady_duration(startTime, time_steady_clock());
  log_i(
      "Ecs system-graph computed",
      log_param("tasks", fmt_int(taskCount)),
      log_param("task-span", fmt_int(bestSpan)),
      log_param("parallelism", fmt_float((f32)taskCount / (f32)bestSpan)),
      log_param("duration", fmt_duration(duration)));

  alloc_free_array_t(runner->alloc, systems, systemCount * 2);
}

EcsRunner* ecs_runner_create(Allocator* alloc, EcsWorld* world, const EcsRunnerFlags flags) {
  const EcsDef* def         = ecs_world_def(world);
  const u32     systemCount = (u32)def->systems.size;
  const u32     taskCount   = systemCount + graph_meta_task_count;

  EcsRunner* runner = alloc_alloc_t(alloc, EcsRunner);

  *runner = (EcsRunner){
      .world          = world,
      .graph          = jobs_graph_create(alloc, string_lit("ecs_runner"), taskCount),
      .flags          = flags,
      .systemTaskSets = alloc_array_t(alloc, EcsTaskSet, systemCount),
      .alloc          = alloc,
  };

  runner_compute_graph(runner, systemCount);
  diag_assert(jobs_graph_task_count(runner->graph) == taskCount);

  // Dump a 'Graph Description Language' aka GraphViz file of the graph to disk if requested.
  if (flags & EcsRunnerFlags_DumpGraphDot) {
    graph_dump_dot(runner);
  }

  // Allocate the runtime memory required to run the graph (reused for every run).
  // NOTE: +64 for bump allocator overhead.
  const usize jobMemSize = jobs_scheduler_mem_size(runner->graph) + 64;
  runner->jobMem         = alloc_alloc(alloc, jobMemSize, jobs_scheduler_mem_align(runner->graph));
  return runner;
}

void ecs_runner_destroy(EcsRunner* runner) {
  diag_assert_msg(!ecs_running(runner), "Runner is still running");

  const EcsDef* def = ecs_world_def(runner->world);
  alloc_free_array_t(runner->alloc, runner->systemTaskSets, def->systems.size);
  jobs_graph_destroy(runner->graph);
  alloc_free(runner->alloc, runner->jobMem);
  alloc_free_t(runner->alloc, runner);
}

const JobGraph* ecs_runner_graph(const EcsRunner* runner) { return runner->graph; }

EcsTaskSet ecs_runner_task_set(const EcsRunner* runner, const EcsSystemId systemId) {
  return runner->systemTaskSets[systemId];
}

bool ecs_running(const EcsRunner* runner) {
  return (runner->flags & EcsRunnerPrivateFlags_Running) != 0;
}

JobId ecs_run_async(EcsRunner* runner) {
  diag_assert_msg(!ecs_running(runner), "Runner is currently already running");
  Allocator* jobAlloc = alloc_bump_create(runner->jobMem);

  runner->flags |= EcsRunnerPrivateFlags_Running;
  ecs_world_busy_set(runner->world);
  return jobs_scheduler_run(runner->graph, jobAlloc);
}

void ecs_run_sync(EcsRunner* runner) {
  const JobId job = ecs_run_async(runner);
  jobs_scheduler_wait_help(job);
}
