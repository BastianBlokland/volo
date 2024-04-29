#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_shuffle.h"
#include "core_sort.h"
#include "core_time.h"
#include "ecs_runner.h"
#include "jobs_executor.h"
#include "jobs_graph.h"
#include "jobs_scheduler.h"
#include "trace_tracer.h"

#include "view_internal.h"
#include "world_internal.h"

/**
 * Meta systems:
 * - Replan (attempt to compute a more efficient execution plan).
 * - Flush (applies entity layout modifications).
 */
#define graph_meta_task_count 2

static const f64 g_runnerInvAvgWindow = 1.0 / 15.0;

typedef EcsSystemDef* EcsSystemDefPtr;

typedef enum {
  EcsRunnerPrivateFlags_Running = 1 << (EcsRunnerFlags_Count + 0),
} EcsRunnerPrivateFlags;

typedef struct {
  TimeDuration dur;
} TaskScratchpad;

typedef struct {
  EcsRunner* runner;
} TaskContextMeta;

typedef struct {
  EcsSystemId      id;
  u16              parCount, parIndex;
  const EcsRunner* runner;
  EcsWorld*        world;
  EcsSystemRoutine routine;
} TaskContextSystem;

typedef struct {
  JobGraph*   graph;
  EcsTaskSet* systemTasks; // EcsTaskSet[systemCount].
  EcsTaskSet  replanTasks, flushTasks;
} RunnerPlan;

typedef struct {
  TimeDuration totalDurAvg;
} RunnerSystemStats;

struct sEcsRunner {
  Allocator*         alloc;
  EcsWorld*          world;
  u32                flags;
  u32                planIndex, planIndexNext;
  RunnerPlan         plans[2];
  BitSet             conflictMatrix; // Triangular matrix of sys conflicts. bit[systemId, systemId].
  RunnerSystemStats* stats;          // RunnerSystemStats[systemCount].
  TimeDuration       replanDurAvg, flushDurAvg;
  Mem                jobMem;
};

THREAD_LOCAL bool             g_ecsRunningSystem;
THREAD_LOCAL EcsSystemId      g_ecsRunningSystemId = sentinel_u16;
THREAD_LOCAL const EcsRunner* g_ecsRunningRunner;

static u32  runner_plan_pick(EcsRunner*);
static void runner_plan_formulate(EcsRunner*, const u32 planIndex, const bool shuffle);

static i8 compare_system_entry(const void* a, const void* b) {
  const EcsSystemDef* const* entryA = a;
  const EcsSystemDef* const* entryB = b;
  return compare_i32(&(*entryA)->order, &(*entryB)->order);
}

static void runner_avg_dur(TimeDuration* value, const TimeDuration new) {
  *value += (TimeDuration)((new - *value) * g_runnerInvAvgWindow);
}

static bool runner_taskset_contains(const EcsTaskSet set, const JobTaskId task) {
  return task >= set.begin && task < set.end;
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

static void runner_task_replan(const void* ctx) {
  const TaskContextMeta* ctxMeta = ctx;
  EcsRunner*             runner  = ctxMeta->runner;

  if (g_jobsWorkerCount == 1) {
    return; // Replanning (to improve parallelism) only makes sense if we have multiple workers.
  }
  if (!(runner->flags & EcsRunnerFlags_Replan)) {
    return; // Replan not enabled.
  }

  const TimeSteady startTime       = time_steady_clock();
  const u32        planIndexActive = runner->planIndex;
  const u32        planIndexIdle   = planIndexActive ^ 1;

  /**
   * Re-formulate the idle plan.
   * Currently we always start from a fully random order (by shuffling the systems), then build
   * the plan, estimate the cost and determine if its better then the current plan.
   */
  runner_plan_formulate(runner, planIndexIdle, true /* shuffle */);

  // If the plan is better then set it as the next plan.
  if (runner_plan_pick(runner) == planIndexIdle) {
    runner->planIndexNext = planIndexIdle;
  }

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  runner_avg_dur(&runner->replanDurAvg, math_max(dur, 1));
}

static void runner_task_flush_stats(EcsRunner* runner, const u32 planIndex) {
  const EcsDef*     def  = ecs_world_def(runner->world);
  const RunnerPlan* plan = &runner->plans[planIndex];

  const u32 systemCount = ecs_def_system_count(def);
  for (EcsSystemId sys = 0; sys != systemCount; ++sys) {
    const EcsTaskSet tasks = plan->systemTasks[sys];

    TimeDuration totalDur = 0;
    for (JobTaskId task = tasks.begin; task != tasks.end; ++task) {
      TaskScratchpad* taskScratchpad = jobs_scratchpad(task).ptr;
      totalDur += taskScratchpad->dur;
    }

    runner_avg_dur(&runner->stats[sys].totalDurAvg, totalDur);
  }
}

static void runner_task_flush(const void* ctx) {
  const TaskContextMeta* ctxMeta   = ctx;
  const TimeSteady       startTime = time_steady_clock();

  ecs_world_flush_internal(ctxMeta->runner->world);

  ctxMeta->runner->flags &= ~EcsRunnerPrivateFlags_Running;
  ecs_world_busy_unset(ctxMeta->runner->world);

  runner_task_flush_stats(ctxMeta->runner, ctxMeta->runner->planIndex);

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  runner_avg_dur(&ctxMeta->runner->flushDurAvg, math_max(dur, 1));
}

static void runner_task_system(const void* context) {
  const TaskContextSystem* ctxSys     = context;
  TaskScratchpad*          scratchpad = jobs_scratchpad(g_jobsTaskId).ptr;
  const TimeSteady         startTime  = time_steady_clock();

  g_ecsRunningSystem   = true;
  g_ecsRunningSystemId = ctxSys->id;
  g_ecsRunningRunner   = ctxSys->runner;

  ctxSys->routine(ctxSys->world, ctxSys->parCount, ctxSys->parIndex);

  g_ecsRunningSystem   = false;
  g_ecsRunningSystemId = sentinel_u16;
  g_ecsRunningRunner   = null;

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  scratchpad->dur        = math_max(dur, 1);
}

static EcsTaskSet runner_insert_replan(EcsRunner* runner, const u32 planIndex) {
  const RunnerPlan* plan = &runner->plans[planIndex];
  /**
   * Insert a task to attempt to compute a more efficient execution plan.
   */
  const JobTaskId taskId = jobs_graph_add_task(
      plan->graph,
      string_lit("Replan"),
      runner_task_replan,
      mem_struct(TaskContextMeta, .runner = runner),
      JobTaskFlags_None);

  return (EcsTaskSet){.begin = taskId, .end = taskId + 1};
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
      mem_struct(TaskContextMeta, .runner = runner),
      JobTaskFlags_ThreadAffinity);

  return (EcsTaskSet){.begin = taskId, .end = taskId + 1};
}

static EcsTaskSet runner_insert_system(
    EcsRunner*          runner,
    const u32           planIndex,
    const EcsSystemId   systemId,
    const EcsSystemDef* systemDef) {
  const RunnerPlan* plan = &runner->plans[planIndex];

  u16 parallelCount = systemDef->parallelCount;
  if (g_jobsWorkerCount == 1) {
    parallelCount = 1; // Parallel systems only makes sense if we have multiple workers.
  }

  JobTaskId firstTaskId = 0;
  for (u16 parIndex = 0; parIndex != parallelCount; ++parIndex) {
    const JobTaskId taskId = jobs_graph_add_task(
        plan->graph,
        systemDef->name,
        runner_task_system,
        mem_struct(
            TaskContextSystem,
            .id       = systemId,
            .parCount = parallelCount,
            .parIndex = parIndex,
            .runner   = runner,
            .world    = runner->world,
            .routine  = systemDef->routine),
        runner_task_system_flags(systemDef));

    if (parIndex == 0) {
      firstTaskId = taskId;
    }
  }
  return (EcsTaskSet){.begin = firstTaskId, .end = firstTaskId + parallelCount};
}

static bool runner_conflict_compute(EcsWorld* world, const EcsSystemDef* a, const EcsSystemDef* b) {
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

static BitSet runner_conflict_matrix_create(EcsWorld* world, Allocator* alloc) {
  /**
   * Construct a strictly triangular matrix of system conflict bits. This allows for fast querying
   * if two systems conflict.
   *
   * Example matrix (with system a, b, c, d, e):
   *   a b c d e
   * a - - - - -
   * b 0 - - - -
   * c 0 1 - - -
   * d 1 0 1 - -
   * e 0 1 0 0 -
   *
   * This encodes the following conflicts:
   *  a <-> d
   *  b <-> c
   *  b <-> e
   *  c <-> d
   */
  const EcsDef* def         = ecs_world_def(world);
  const u32     systemCount = ecs_def_system_count(def);
  if (systemCount < 2) {
    return mem_empty; // No conflicts are possible with less then two systems.
  }

  const u32    bitCount = systemCount * (systemCount - 1) / 2; // Strict triangular matrix entries.
  const BitSet matrix   = alloc_alloc(alloc, bits_to_bytes(bitCount) + 1, 1);
  bitset_clear_all(matrix);

  u32 bitIndex = 0;
  for (EcsSystemId sysA = 0; sysA != systemCount; ++sysA) {
    const EcsSystemDef* sysADef = dynarray_at_t(&def->systems, sysA, EcsSystemDef);
    for (EcsSystemId sysB = 0; sysB != sysA; ++sysB, ++bitIndex) {
      diag_assert(bitIndex < bitCount);

      const EcsSystemDef* sysBDef = dynarray_at_t(&def->systems, sysB, EcsSystemDef);
      if (runner_conflict_compute(world, sysADef, sysBDef)) {
        bitset_set(matrix, bitIndex);
      }
    }
  }

  return matrix;
}

static bool runner_conflict_query(const BitSet conflictMatrix, EcsSystemId a, EcsSystemId b) {
  if (a < b) {
    const EcsSystemId tmp = a;
    a                     = b;
    b                     = tmp;
  }
  const u32 bitIndex = (a * (a - 1) / 2) + b; // Strict triangular matrix.
  return bitset_test(conflictMatrix, bitIndex);
}

static void runner_system_collect(const EcsDef* def, EcsSystemDefPtr out[]) {
  for (EcsSystemId sysId = 0; sysId != def->systems.size; ++sysId) {
    out[sysId] = dynarray_at_t(&def->systems, sysId, EcsSystemDef);
  }
}

typedef struct {
  EcsRunner* runner;
  u32        planIndex;
} RunnerEstimateContext;

static u64 runner_plan_cost_estimate(const void* userCtx, const JobTaskId task) {
  const RunnerEstimateContext* ctx  = (const RunnerEstimateContext*)userCtx;
  const EcsDef*                def  = ecs_world_def(ctx->runner->world);
  const RunnerPlan*            plan = &ctx->runner->plans[ctx->planIndex];

  if (runner_taskset_contains(plan->replanTasks, task)) {
    return ctx->runner->replanDurAvg;
  }
  if (runner_taskset_contains(plan->flushTasks, task)) {
    return ctx->runner->flushDurAvg;
  }
  // Task is not one of the meta tasks; assume its a system.
  const TaskContextSystem* sysTaskCtx     = jobs_graph_task_ctx(plan->graph, task).ptr;
  const TimeDuration       sysTotalDurAvg = ctx->runner->stats[sysTaskCtx->id].totalDurAvg;
  return sysTotalDurAvg / ecs_def_system_parallel(def, sysTaskCtx->id);
}

static u32 runner_plan_pick(EcsRunner* runner) {
  u32 bestIndex = 0;
  u64 bestCost  = u64_max;

  trace_begin("ecs_plan_pick", TraceColor_Blue);

  for (u32 i = 0; i != array_elems(runner->plans); ++i) {
    const RunnerPlan* plan = &runner->plans[i];

    // Compute the plan cost (longest path through the graph).
    // Estimation of the theoretical shortest runtime in nano-seconds (given infinite parallelism).
    const RunnerEstimateContext ctx = {.runner = runner, .planIndex = i};
    const u64 cost = jobs_graph_task_span_cost(plan->graph, runner_plan_cost_estimate, &ctx);
    if (cost < bestCost) {
      bestIndex = i;
      bestCost  = cost;
    }
  }

  trace_end();
  return bestIndex;
}

static void runner_plan_formulate(EcsRunner* runner, const u32 planIndex, const bool shuffle) {
  const EcsDef* def  = ecs_world_def(runner->world);
  RunnerPlan*   plan = &runner->plans[planIndex];

  const u32        systemCount = ecs_def_system_count(def);
  EcsSystemDefPtr* systems     = mem_stack(sizeof(EcsSystemDefPtr) * systemCount).ptr;

  trace_begin("ecs_plan_collect", TraceColor_Blue);

  // Find all the registered systems.
  runner_system_collect(def, systems);

  // Optionally shuffle them.
  if (shuffle) {
    shuffle_fisheryates_t(g_rng, systems, systems + systemCount, EcsSystemDefPtr);
  }

  // Sort the systems to respect the ordering constrains.
  sort_bubblesort_t(systems, systems + systemCount, EcsSystemDefPtr, compare_system_entry);

  trace_end();
  trace_begin("ecs_plan_build", TraceColor_Blue);

  // Insert the systems into the job-graph.
  jobs_graph_clear(plan->graph);

  // Insert meta tasks.
  plan->replanTasks = runner_insert_replan(runner, planIndex);
  plan->flushTasks  = runner_insert_flush(runner, planIndex);

  // Insert system tasks.
  for (EcsSystemDefPtr* sysDef = systems; sysDef != systems + systemCount; ++sysDef) {
    const EcsSystemId sysId      = ecs_def_system_id(def, *sysDef);
    const EcsTaskSet  entryTasks = runner_insert_system(runner, planIndex, sysId, *sysDef);
    plan->systemTasks[sysId]     = entryTasks;

    // Insert a flush dependency (so flush only happens when all systems are done).
    runner_add_dep(plan->graph, entryTasks, plan->flushTasks);

    // Insert required dependencies on the earlier systems.
    for (EcsSystemDefPtr* earlierSysDef = systems; earlierSysDef != sysDef; ++earlierSysDef) {
      const EcsSystemId earlierSysId = ecs_def_system_id(def, *earlierSysDef);
      if (runner_conflict_query(runner->conflictMatrix, sysId, earlierSysId)) {
        runner_add_dep(plan->graph, plan->systemTasks[earlierSysId], entryTasks);
      }
    }
  }

  trace_end();
  trace_begin("ecs_plan_finalize", TraceColor_Blue);

  jobs_graph_reduce_dependencies(plan->graph);

  trace_end();
}

EcsRunner* ecs_runner_create(Allocator* alloc, EcsWorld* world, const EcsRunnerFlags flags) {
  const EcsDef* def         = ecs_world_def(world);
  const u32     systemCount = (u32)def->systems.size;

  const u32 expectedParallelism = 2;
  const u32 expectedTaskCount   = (systemCount * expectedParallelism) + graph_meta_task_count;

  EcsRunner* runner = alloc_alloc_t(alloc, EcsRunner);

  *runner = (EcsRunner){
      .alloc          = alloc,
      .world          = world,
      .flags          = flags,
      .conflictMatrix = runner_conflict_matrix_create(world, alloc),
  };

  if (systemCount) {
    runner->stats = alloc_array_t(alloc, RunnerSystemStats, systemCount);
    mem_set(mem_create(runner->stats, sizeof(RunnerSystemStats) * systemCount), 0);
  }

  array_for_t(runner->plans, RunnerPlan, plan) {
    plan->graph       = jobs_graph_create(alloc, string_lit("ecs_runner"), expectedTaskCount);
    plan->systemTasks = systemCount ? alloc_array_t(alloc, EcsTaskSet, systemCount) : null;
  }

  runner_plan_formulate(runner, runner->planIndex, false /* shuffle */);

  // Allocate the runtime memory required to run the graph (reused for every run).
  // NOTE: +64 for bump allocator overhead.
  const JobGraph* graph      = runner->plans[runner->planIndex].graph;
  const usize     jobMemSize = jobs_scheduler_mem_size(graph) + 64;
  runner->jobMem             = alloc_alloc(alloc, jobMemSize, jobs_scheduler_mem_align(graph));
  return runner;
}

void ecs_runner_destroy(EcsRunner* runner) {
  diag_assert_msg(!ecs_running(runner), "Runner is still running");

  const EcsDef* def         = ecs_world_def(runner->world);
  const u32     systemCount = ecs_def_system_count(def);

  array_for_t(runner->plans, RunnerPlan, plan) {
    jobs_graph_destroy(plan->graph);
    if (plan->systemTasks) {
      alloc_free_array_t(runner->alloc, plan->systemTasks, systemCount);
    }
  }
  if (mem_valid(runner->conflictMatrix)) {
    alloc_free(runner->alloc, runner->conflictMatrix);
  }
  if (runner->stats) {
    alloc_free_array_t(runner->alloc, runner->stats, systemCount);
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

TimeDuration ecs_runner_duration_avg(const EcsRunner* runner, const EcsSystemId systemId) {
  return runner->stats[systemId].totalDurAvg;
}

bool ecs_running(const EcsRunner* runner) {
  return (runner->flags & EcsRunnerPrivateFlags_Running) != 0;
}

JobId ecs_run_async(EcsRunner* runner) {
  diag_assert_msg(!ecs_running(runner), "Runner is currently already running");
  Allocator* jobAlloc = alloc_bump_create(runner->jobMem);

  runner->flags |= EcsRunnerPrivateFlags_Running;
  ecs_world_busy_set(runner->world);

  runner->planIndex = runner->planIndexNext;
  return jobs_scheduler_run(runner->plans[runner->planIndex].graph, jobAlloc);
}

void ecs_run_sync(EcsRunner* runner) {
  const JobId job = ecs_run_async(runner);
  jobs_scheduler_wait_help(job);
}
