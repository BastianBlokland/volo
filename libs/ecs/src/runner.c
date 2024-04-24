#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_rng.h"
#include "core_shuffle.h"
#include "core_sort.h"
#include "core_time.h"
#include "ecs_runner.h"
#include "jobs_graph.h"
#include "jobs_scheduler.h"

#include "view_internal.h"
#include "world_internal.h"

/**
 * Meta systems:
 * - Flush (applies entity layout modifications).
 */
#define graph_meta_task_count 1

typedef EcsSystemDef* EcsSystemDefPtr;

typedef enum {
  EcsRunnerPrivateFlags_Running = 1 << (EcsRunnerFlags_Count + 0),
} EcsRunnerPrivateFlags;

typedef struct {
  EcsRunner* runner;
} RunnerTaskMeta;

typedef struct {
  EcsSystemId      id;
  u16              parCount, parIndex;
  const EcsRunner* runner;
  EcsWorld*        world;
  EcsSystemRoutine routine;
} RunnerTaskSystem;

typedef struct {
  JobGraph*   graph;
  EcsTaskSet* systemTasks;
  u32         cost;
} RunnerPlan;

struct sEcsRunner {
  EcsWorld*        world;
  u32              flags;
  u32              systemCount;
  EcsSystemDefPtr* systems; // EcsSystemDefPtr[systemCount].
  RunnerPlan       plans[2];
  u32              planIndex;
  Allocator*       alloc;
  Mem              jobMem;
};

THREAD_LOCAL bool        g_ecsRunningSystem;
THREAD_LOCAL EcsSystemId g_ecsRunningSystemId = sentinel_u16;
THREAD_LOCAL const EcsRunner* g_ecsRunningRunner;

static i8 compare_system_entry(const void* a, const void* b) {
  const EcsSystemDefPtr* entryA = a;
  const EcsSystemDefPtr* entryB = b;
  return compare_i32(&(*entryA)->order, &(*entryB)->order);
}

/**
 * Add a dependency between the parent and child tasks. The child tasks are only allowed to start
 * once all parent tasks have finished.
 */
static void runner_add_dep(JobGraph* graph, const EcsTaskSet parent, const EcsTaskSet child) {
  for (JobTaskId parentTaskId = parent.begin; parentTaskId != parent.end; ++parentTaskId) {
    for (JobTaskId childTaskId = child.begin; childTaskId != child.end; ++childTaskId) {
      jobs_graph_task_depend(graph, parentTaskId, childTaskId);
    }
  }
}

static JobTaskFlags runner_task_system_flags(const EcsSystemDef* systemDef) {
  JobTaskFlags flags = JobTaskFlags_None;
  if (systemDef->flags & EcsSystemFlags_ThreadAffinity) {
    flags |= JobTaskFlags_ThreadAffinity;
  }
  return flags;
}

static void runner_task_flush(void* context) {
  const RunnerTaskMeta* data = context;
  ecs_world_flush_internal(data->runner->world);

  data->runner->flags &= ~EcsRunnerPrivateFlags_Running;
  ecs_world_busy_unset(data->runner->world);
}

static void runner_task_system(void* context) {
  const RunnerTaskSystem* data = context;

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

static EcsTaskSet runner_insert_flush(EcsRunner* runner, const u32 planIndex) {
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
      runner_task_flush,
      mem_struct(RunnerTaskMeta, .runner = runner),
      JobTaskFlags_ThreadAffinity);

  return (EcsTaskSet){.begin = taskId, .end = taskId + 1};
}

static EcsTaskSet runner_insert_system(
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
        runner_task_system,
        mem_struct(
            RunnerTaskSystem,
            .id       = systemId,
            .parCount = systemDef->parallelCount,
            .parIndex = parIndex,
            .runner   = runner,
            .world    = runner->world,
            .routine  = systemDef->routine),
        runner_task_system_flags(systemDef));

    if (parIndex == 0) {
      firstTaskId = taskId;
    }
  }
  return (EcsTaskSet){.begin = firstTaskId, .end = firstTaskId + systemDef->parallelCount};
}

static bool runner_system_conflict(EcsWorld* world, const EcsSystemDef* a, const EcsSystemDef* b) {
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

static void runner_system_collect(const EcsDef* def, EcsSystemDefPtr out[]) {
  for (EcsSystemId sysId = 0; sysId != def->systems.size; ++sysId) {
    out[sysId] = dynarray_at_t(&def->systems, sysId, EcsSystemDef);
  }
}

static void runner_plan_formulate(EcsRunner* runner, const u32 planIndex, const bool shuffle) {
  const EcsDef*    def      = ecs_world_def(runner->world);
  EcsSystemDefPtr* sysBegin = runner->systems;
  EcsSystemDefPtr* sysEnd   = runner->systems + runner->systemCount;
  RunnerPlan*      plan     = &runner->plans[planIndex];

  // Optionally start with a random system order.
  if (shuffle) {
    shuffle_fisheryates_t(g_rng, sysBegin, sysEnd, EcsSystemDefPtr);
  }

  // Sort the systems to respect the ordering constrains.
  sort_bubblesort_t(sysBegin, sysEnd, EcsSystemDefPtr, compare_system_entry);

  // Insert the systems into a job-graph.
  jobs_graph_clear(plan->graph);
  const EcsTaskSet flushTask = runner_insert_flush(runner, planIndex);
  for (EcsSystemDefPtr* sysDef = sysBegin; sysDef != sysEnd; ++sysDef) {
    const EcsSystemId sysId      = ecs_def_system_id(def, *sysDef);
    const EcsTaskSet  entryTasks = runner_insert_system(runner, planIndex, sysId, *sysDef);
    plan->systemTasks[sysId]     = entryTasks;

    // Insert a flush dependency (so flush only happens when all systems are done).
    runner_add_dep(plan->graph, entryTasks, flushTask);

    // Insert required dependencies on the earlier systems.
    for (EcsSystemDefPtr* earlierSysDef = sysBegin; earlierSysDef != sysDef; ++earlierSysDef) {
      const EcsSystemId earlierSysId = ecs_def_system_id(def, *earlierSysDef);
      if (runner_system_conflict(runner->world, *sysDef, *earlierSysDef)) {
        runner_add_dep(plan->graph, plan->systemTasks[earlierSysId], entryTasks);
      }
    }
  }

  // Compute the plan cost (longest path through the graph).
  plan->cost = jobs_graph_task_span(plan->graph);
}

static void runner_plan_optimize(EcsRunner* runner, const u32 planIndex) {
  const RunnerPlan* plan = &runner->plans[planIndex];
  jobs_graph_reduce_dependencies(plan->graph);
}

/**
 * Find the plan with the lowest cost.
 */
static u32 runner_plan_best(EcsRunner* runner) {
  u32 bestPlan = sentinel_u32;
  u32 bestCost = u32_max;
  for (u32 i = 0; i != array_elems(runner->plans); ++i) {
    if (runner->plans[i].cost < bestCost) {
      bestPlan = i;
      bestCost = runner->plans[i].cost;
    }
  }
  return bestPlan;
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
      .systems     = alloc_array_t(alloc, EcsSystemDefPtr, systemCount),
      .alloc       = alloc,
  };
  array_for_t(runner->plans, RunnerPlan, plan) {
    plan->graph       = jobs_graph_create(alloc, string_lit("ecs_runner"), taskCount);
    plan->systemTasks = alloc_array_t(alloc, EcsTaskSet, systemCount);
    plan->cost        = u32_max;
  }

  runner_system_collect(def, runner->systems);
  runner_plan_formulate(runner, runner->planIndex, false /* shuffle */);
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

  // Pick the plan to schedule.
  runner->planIndex = runner_plan_best(runner);

  // Schedule the plan.
  return jobs_scheduler_run(runner->plans[runner->planIndex].graph, jobAlloc);
}

void ecs_run_sync(EcsRunner* runner) {
  const JobId job = ecs_run_async(runner);
  jobs_scheduler_wait_help(job);
}
