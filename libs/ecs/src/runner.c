#include "core_alloc.h"
#include "core_array.h"
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

typedef enum {
  EcsRunnerPrivateFlags_Running = 1 << (EcsRunnerFlags_Count + 0),
} EcsRunnerPrivateFlags;

typedef struct {
  EcsRunner* runner;
} MetaTaskData;

typedef struct {
  EcsSystemId      id;
  u16              parCount, parIndex;
  const EcsRunner* runner;
  EcsWorld*        world;
  EcsSystemRoutine routine;
} SystemTaskData;

typedef struct {
  EcsSystemId   id;
  i32           order;
  EcsSystemDef* def;
} RunnerSystemEntry;

typedef struct {
  JobGraph*   graph;
  EcsTaskSet* systemTasks;
} RunnerPlan;

struct sEcsRunner {
  EcsWorld*          world;
  u32                flags;
  u32                systemCount;
  RunnerSystemEntry* systems; // RunnerSystemEntry[systemCount].
  RunnerPlan         plans[2];
  u32                planIndex;
  Allocator*         alloc;
  Mem                jobMem;
};

THREAD_LOCAL bool             g_ecsRunningSystem;
THREAD_LOCAL EcsSystemId      g_ecsRunningSystemId = sentinel_u16;
THREAD_LOCAL const EcsRunner* g_ecsRunningRunner;

static i8 compare_system_entry(const void* a, const void* b) {
  return compare_i32(
      field_ptr(a, RunnerSystemEntry, order), field_ptr(b, RunnerSystemEntry, order));
}

/**
 * Add a dependency between the parent and child tasks. The child tasks are only allowed to start
 * once all parent tasks have finished.
 */
static void graph_add_dep(JobGraph* graph, const EcsTaskSet parent, const EcsTaskSet child) {
  for (JobTaskId parentTaskId = parent.begin; parentTaskId != parent.end; ++parentTaskId) {
    for (JobTaskId childTaskId = child.begin; childTaskId != child.end; ++childTaskId) {
      jobs_graph_task_depend(graph, parentTaskId, childTaskId);
    }
  }
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

  data->routine(data->world, data->parCount, data->parIndex);

  g_ecsRunningSystem   = false;
  g_ecsRunningSystemId = sentinel_u16;
  g_ecsRunningRunner   = null;

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  ecs_world_stats_sys_add(data->world, data->id, dur);
}

static EcsTaskSet graph_insert_flush(EcsRunner* runner, const u32 planIndex) {
  const RunnerPlan* plan = &runner->plans[planIndex];
  /**
   * Insert a task to flush the world (applies entity layout modifications).
   *
   * NOTE: Register the job with 'ThreadAffinity' to handle component destructors that need to be
   * ran on the same thread as its systems (because they need to cleanup thread-local data).
   * This is unfortunately hard to avoid with some of the win32 apis that use thread-local queues.
   */
  const JobTaskId taskId = jobs_graph_add_task(
      plan->graph,
      string_lit("Flush"),
      graph_runner_flush_task,
      mem_struct(MetaTaskData, .runner = runner),
      JobTaskFlags_ThreadAffinity);

  return (EcsTaskSet){.begin = taskId, .end = taskId + 1};
}

static EcsTaskSet graph_insert_system(
    EcsRunner*          runner,
    const u32           planIndex,
    const EcsSystemId   systemId,
    const EcsSystemDef* systemDef) {
  const RunnerPlan* plan = &runner->plans[planIndex];

  JobTaskId firstTaskId = 0;
  for (u16 parIndex = 0; parIndex != systemDef->parallelCount; ++parIndex) {
    const JobTaskId taskId = jobs_graph_add_task(
        plan->graph,
        systemDef->name,
        graph_system_task,
        mem_struct(
            SystemTaskData,
            .id       = systemId,
            .parCount = systemDef->parallelCount,
            .parIndex = parIndex,
            .runner   = runner,
            .world    = runner->world,
            .routine  = systemDef->routine),
        graph_system_task_flags(systemDef));

    if (parIndex == 0) {
      firstTaskId = taskId;
    }
  }
  return (EcsTaskSet){.begin = firstTaskId, .end = firstTaskId + systemDef->parallelCount};
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

static void runner_systems_collect(EcsRunner* runner) {
  const EcsDef* def = ecs_world_def(runner->world);
  for (EcsSystemId sysId = 0; sysId != runner->systemCount; ++sysId) {
    EcsSystemDef* sysDef = dynarray_at_t(&def->systems, sysId, EcsSystemDef);

    runner->systems[sysId] = (RunnerSystemEntry){
        .id    = sysId,
        .order = sysDef->order,
        .def   = sysDef,
    };
  }
}

static void runner_plan_formulate(EcsRunner* runner, const u32 planIndex) {
  const RunnerPlan*        plan     = &runner->plans[planIndex];
  const RunnerSystemEntry* sysBegin = runner->systems;
  const RunnerSystemEntry* sysEnd   = runner->systems + runner->systemCount;

  // Sort the systems to respect the ordering constrains.
  sort_bubblesort_t(sysBegin, sysEnd, RunnerSystemEntry, compare_system_entry);

  // Insert the systems into a job-graph.
  jobs_graph_clear(plan->graph);
  const EcsTaskSet flushTask = graph_insert_flush(runner, planIndex);
  for (const RunnerSystemEntry* entry = sysBegin; entry != sysEnd; ++entry) {
    const EcsTaskSet entryTasks  = graph_insert_system(runner, planIndex, entry->id, entry->def);
    plan->systemTasks[entry->id] = entryTasks;

    // Insert a flush dependency (so flush only happens when all systems are done).
    graph_add_dep(plan->graph, entryTasks, flushTask);

    // Insert required dependencies on the earlier systems.
    for (const RunnerSystemEntry* earlierEntry = sysBegin; earlierEntry != entry; ++earlierEntry) {
      if (graph_system_conflict(runner->world, entry->def, earlierEntry->def)) {
        graph_add_dep(plan->graph, plan->systemTasks[earlierEntry->id], entryTasks);
      }
    }
  }
}

static void runner_plan_optimize(EcsRunner* runner, const u32 planIndex) {
  const RunnerPlan* plan = &runner->plans[planIndex];
  jobs_graph_reduce_dependencies(plan->graph);
}

EcsRunner* ecs_runner_create(Allocator* alloc, EcsWorld* world, const EcsRunnerFlags flags) {
  const EcsDef* def         = ecs_world_def(world);
  const u32     systemCount = (u32)def->systems.size;
  const u32     taskCount   = systemCount + graph_meta_task_count;

  EcsRunner* runner = alloc_alloc_t(alloc, EcsRunner);

  *runner = (EcsRunner){
      .world       = world,
      .flags       = flags,
      .systemCount = systemCount,
      .systems     = alloc_array_t(alloc, RunnerSystemEntry, systemCount),
      .alloc       = alloc,
  };
  array_for_t(runner->plans, RunnerPlan, plan) {
    plan->graph       = jobs_graph_create(alloc, string_lit("ecs_runner"), taskCount);
    plan->systemTasks = alloc_array_t(alloc, EcsTaskSet, systemCount);
  }

  runner_systems_collect(runner);
  runner_plan_formulate(runner, runner->planIndex);
  runner_plan_optimize(runner, runner->planIndex);

  // Allocate the runtime memory required to run the graph (reused for every run).
  // NOTE: +64 for bump allocator overhead.
  const JobGraph* graph      = runner->plans[runner->planIndex].graph;
  const usize     jobMemSize = jobs_scheduler_mem_size(graph) + 64;
  runner->jobMem             = alloc_alloc(alloc, jobMemSize, jobs_scheduler_mem_align(graph));
  return runner;
}

void ecs_runner_destroy(EcsRunner* runner) {
  diag_assert_msg(!ecs_running(runner), "Runner is still running");

  alloc_free_array_t(runner->alloc, runner->systems, runner->systemCount);
  array_for_t(runner->plans, RunnerPlan, plan) {
    jobs_graph_destroy(plan->graph);
    alloc_free_array_t(runner->alloc, plan->systemTasks, runner->systemCount);
  }
  alloc_free(runner->alloc, runner->jobMem);
  alloc_free_t(runner->alloc, runner);
}

const JobGraph* ecs_runner_graph(const EcsRunner* runner) {
  const RunnerPlan* activePlan = &runner->plans[runner->planIndex];
  return activePlan->graph;
}

EcsTaskSet ecs_runner_task_set(const EcsRunner* runner, const EcsSystemId systemId) {
  const RunnerPlan* activePlan = &runner->plans[runner->planIndex];
  return activePlan->systemTasks[systemId];
}

bool ecs_running(const EcsRunner* runner) {
  return (runner->flags & EcsRunnerPrivateFlags_Running) != 0;
}

JobId ecs_run_async(EcsRunner* runner) {
  diag_assert_msg(!ecs_running(runner), "Runner is currently already running");
  Allocator* jobAlloc = alloc_bump_create(runner->jobMem);

  runner->flags |= EcsRunnerPrivateFlags_Running;
  ecs_world_busy_set(runner->world);

  const RunnerPlan* activePlan = &runner->plans[runner->planIndex];
  return jobs_scheduler_run(activePlan->graph, jobAlloc);
}

void ecs_run_sync(EcsRunner* runner) {
  const JobId job = ecs_run_async(runner);
  jobs_scheduler_wait_help(job);
}
