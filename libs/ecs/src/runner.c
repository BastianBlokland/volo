#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_shuffle.h"
#include "core_sort.h"
#include "core_time.h"
#include "ecs_runner.h"
#include "jobs_executor.h"
#include "jobs_graph.h"
#include "jobs_scheduler.h"
#include "log_logger.h"
#include "trace_tracer.h"

#include "view_internal.h"
#include "world_internal.h"

// #define VOLO_ECS_RUNNER_VERBOSE
// #define VOLO_ECS_RUNNER_VALIDATION
// #define VOLO_ECS_RUNNER_STRESS

static const f64 g_runnerInvAvgWindow = 1.0 / 15.0;

typedef const EcsSystemDef* EcsSystemDefPtr;

typedef enum {
  EcsRunnerMetaTask_Replan, // Attempt to compute a more efficient execution plan.
  EcsRunnerMetaTask_Flush,  // Applies entity layout modifications.

  EcsRunnerMetaTask_Count
} EcsRunnerMetaTask;

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
  EcsSystemRoutine routine;
} TaskContextSystem;

typedef struct {
  JobGraph*   graph;
  EcsTaskSet* systemTasks; // EcsTaskSet[systemCount].
  JobTaskId   metaTasks[EcsRunnerMetaTask_Count];
} RunnerPlan;

typedef struct {
  TimeDuration totalDurAvg;
} RunnerSystemStats;

typedef struct {
  TimeDuration durLast, durAvg;
} RunnerMetaStats;

struct sEcsRunner {
  Allocator*         alloc;
  EcsWorld*          world;
  u32                flags;
  u32                taskCount;
  u32                planIndex, planIndexNext;
  RunnerPlan         plans[2];
  BitSet             sysConflicts; // bit[systemId, systemId], triangular matrix of sys conflicts.
  RunnerSystemStats* sysStats;     // RunnerSystemStats[systemCount].
  RunnerMetaStats    metaStats[EcsRunnerMetaTask_Count];
  u64                planCounter;
  TimeDuration       planEstSpan; // Estimated duration of the longest span through the graph.
  Mem                jobMem;
};

THREAD_LOCAL bool             g_ecsRunningSystem;
THREAD_LOCAL EcsSystemId      g_ecsRunningSystemId = sentinel_u16;
THREAD_LOCAL const EcsRunner* g_ecsRunningRunner;

static void runner_plan_pick(EcsRunner*);
static void runner_plan_formulate(EcsRunner*, const u32 planIndex, const bool shuffle);

static i8 compare_system_entry(const void* a, const void* b) {
  const EcsSystemDef* const* entryA = a;
  const EcsSystemDef* const* entryB = b;
  return compare_i32(&(*entryA)->order, &(*entryB)->order);
}

static void runner_avg_dur(TimeDuration* value, const TimeDuration new) {
  *value += (TimeDuration)((new - *value) * g_runnerInvAvgWindow);
}

static u32 runner_task_count_system(const EcsSystemDef* sysDef) {
  if (g_jobsWorkerCount == 1) {
    return 1; // Parallel systems only makes sense if we have multiple workers.
  }
  return sysDef->parallelCount;
}

static u32 runner_task_count_total(const EcsDef* def) {
  const EcsSystemDef* sysDefsBegin = dynarray_begin_t(&def->systems, EcsSystemDef);
  const EcsSystemDef* sysDefsEnd   = dynarray_end_t(&def->systems, EcsSystemDef);

  u32 taskCount = EcsRunnerMetaTask_Count;
  for (const EcsSystemDef* sysDef = sysDefsBegin; sysDef != sysDefsEnd; ++sysDef) {
    taskCount += runner_task_count_system(sysDef);
  }
  return taskCount;
}

static JobTaskFlags runner_task_system_flags(const EcsSystemDef* systemDef) {
  JobTaskFlags flags = JobTaskFlags_BorrowName;
  if (systemDef->flags & EcsSystemFlags_ThreadAffinity) {
    flags |= JobTaskFlags_ThreadAffinity;
  }
  return flags;
}

static void runner_meta_stats_update(RunnerMetaStats* stats, const TimeDuration dur) {
  stats->durLast = math_max(dur, 1);
  runner_avg_dur(&stats->durAvg, stats->durLast);
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
  runner_plan_pick(runner);

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  runner_meta_stats_update(&runner->metaStats[EcsRunnerMetaTask_Replan], dur);
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

    runner_avg_dur(&runner->sysStats[sys].totalDurAvg, totalDur);
  }
}

static void runner_task_flush(const void* ctx) {
  const TaskContextMeta* ctxMeta   = ctx;
  EcsRunner*             runner    = ctxMeta->runner;
  const TimeSteady       startTime = time_steady_clock();

  ecs_world_flush_internal(runner->world);

  runner_task_flush_stats(runner, runner->planIndex);

  runner->flags &= ~EcsRunnerPrivateFlags_Running;
  ecs_world_busy_unset(runner->world);

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  runner_meta_stats_update(&runner->metaStats[EcsRunnerMetaTask_Flush], dur);
}

static void runner_task_system(const void* context) {
  const TaskContextSystem* ctxSys     = context;
  TaskScratchpad*          scratchpad = jobs_scratchpad(g_jobsTaskId).ptr;
  const TimeSteady         startTime  = time_steady_clock();

  g_ecsRunningSystem   = true;
  g_ecsRunningSystemId = ctxSys->id;
  g_ecsRunningRunner   = ctxSys->runner;

  ctxSys->routine(ctxSys->runner->world, ctxSys->parCount, ctxSys->parIndex);

  g_ecsRunningSystem   = false;
  g_ecsRunningSystemId = sentinel_u16;
  g_ecsRunningRunner   = null;

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  scratchpad->dur        = math_max(dur, 1);
}

typedef struct {
  EcsRunner*        runner;
  const RunnerPlan* plan;
} RunnerEstimateContext;

/**
 * Estimate the cost (in nano-seconds) of a task based on the previously recorded average runtime.
 * NOTE: Returns 1 if no stats are known for this task.
 */
static u64 runner_estimate_task(const RunnerEstimateContext* ctx, const JobTaskId task) {
  for (EcsRunnerMetaTask meta = 0; meta != EcsRunnerMetaTask_Count; ++meta) {
    if (task == ctx->plan->metaTasks[meta]) {
      return math_max(ctx->runner->metaStats[task].durAvg, 1);
    }
  }
  // Task is not a meta task; assume its a system.
  const TaskContextSystem* sysTaskCtx     = jobs_graph_task_ctx(ctx->plan->graph, task).ptr;
  const TimeDuration       sysTotalDurAvg = ctx->runner->sysStats[sysTaskCtx->id].totalDurAvg;
  return math_max(sysTotalDurAvg, sysTaskCtx->parCount) / sysTaskCtx->parCount;
}

/**
 * Estimation of the theoretical shortest runtime in nano-seconds (given infinite parallelism).
 */
static u64 runner_estimate_plan(const RunnerEstimateContext* ctx) {
  return jobs_graph_task_span_cost(ctx->plan->graph, (JobsCostEstimator)runner_estimate_task, ctx);
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
      JobTaskFlags_BorrowName);

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
      JobTaskFlags_BorrowName | JobTaskFlags_ThreadAffinity);

  return (EcsTaskSet){.begin = taskId, .end = taskId + 1};
}

static EcsTaskSet runner_insert_system(
    EcsRunner*          runner,
    const u32           planIndex,
    const EcsSystemId   systemId,
    const EcsSystemDef* systemDef) {
  const RunnerPlan* plan = &runner->plans[planIndex];

  const u32 parallelCount = runner_task_count_system(systemDef);

  JobTaskId firstTaskId = 0;
  for (u16 parIndex = 0; parIndex != parallelCount; ++parIndex) {
    const JobTaskId taskId = jobs_graph_add_task(
        plan->graph,
        systemDef->name,
        runner_task_system,
        mem_struct(
            TaskContextSystem,
            .id       = systemId,
            .parCount = (u16)parallelCount,
            .parIndex = parIndex,
            .runner   = runner,
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
  const u32 bitIndex  = (a * (a - 1) / 2) + b; // Strict triangular matrix.
  const u32 byteIndex = bits_to_bytes(bitIndex);
  const u8  byteMask  = 1u << bit_in_byte(bitIndex);
  return (*mem_at_u8(conflictMatrix, byteIndex) & byteMask) != 0;
}

/**
 * Dependency square matrix.
 * Each row is a task and the columns represent the dependents (aka children).
 * NOTE: Diagonal is unused as tasks cannot depend on themselves.
 *
 * Example matrix (with tasks a, b, c, d, e):
 *   a b c d e
 * a 0 0 0 0 0
 * b 1 0 1 0 1
 * c 1 0 0 0 0
 * d 1 1 0 0 0
 * e 1 0 0 0 0
 *
 * This encodes the following dependencies:
 * - a depends on b, c, d, e.
 * - b depends on d.
 * - c and e depend on b.
 */
typedef struct {
  u64* chunks;       // u64[count * chunkStride], square dependency matrix.
  u32  strideBits;   // Aligned to 64.
  u32  strideChunks; // strideBits / 64
  u32  count;        // Task count (size in bits of a single dimension of the matrix).
} RunnerDepMatrix;

static void runner_dep_clear(RunnerDepMatrix* dep) {
  mem_set(mem_create(dep->chunks, dep->count * dep->strideChunks * sizeof(u64)), 0);
}

static bool runner_dep_test(RunnerDepMatrix* dep, const JobTaskId parent, const JobTaskId child) {
  const u64 chunk = dep->chunks[parent * dep->strideChunks + bits_to_dwords(child)];
  const u64 mask  = u64_lit(1) << bit_in_dword(child);
  return (chunk & mask) != 0;
}

/**
 * Add dependency. The child task is only allowed to start once the parent task has finished.
 */
static void runner_dep_add(RunnerDepMatrix* dep, const JobTaskId parent, const JobTaskId child) {
  const u64 mask = u64_lit(1) << bit_in_dword(child);
  dep->chunks[parent * dep->strideChunks + bits_to_dwords(child)] |= mask;
}

/**
 * Add dependency. The child task is only allowed to start once all parent tasks have finished.
 */
static void
runner_dep_add_to_many(RunnerDepMatrix* dep, const EcsTaskSet parents, const JobTaskId child) {
  for (JobTaskId parent = parents.begin; parent != parents.end; ++parent) {
    runner_dep_add(dep, parent, child);
  }
}

/**
 * Add dependency. The children tasks are only allowed to start once all parent tasks have finished.
 */
static void
runner_dep_add_many(RunnerDepMatrix* dep, const EcsTaskSet parents, const EcsTaskSet children) {
  for (JobTaskId parent = parents.begin; parent != parents.end; ++parent) {
    for (JobTaskId child = children.begin; child != children.end; ++child) {
      runner_dep_add(dep, parent, child);
    }
  }
}

/**
 * Dump the dependency matrix to stdout.
 * Vertical axis contains the tasks and horizontal axis their dependent tasks.
 */
MAYBE_UNUSED static void runner_dep_dump(RunnerDepMatrix* dep, const JobGraph* graph) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, 64 * usize_kibibyte, 1);
  DynString buffer     = dynstring_create_over(scratchMem);

  u16 longestName = 0;
  for (JobTaskId task = 0; task != dep->count; ++task) {
    longestName = math_max(longestName, (u16)jobs_graph_task_name(graph, task).size);
  }

  for (JobTaskId parent = 0; parent != dep->count; ++parent) {
    const String name = jobs_graph_task_name(graph, parent);
    fmt_write(&buffer, "{}{} ", fmt_text(name), fmt_padding(longestName - (u16)name.size));
    for (JobTaskId child = 0; child != dep->count; ++child) {
      if (runner_dep_test(dep, parent, child)) {
        dynstring_append_char(&buffer, '1');
      } else {
        dynstring_append_char(&buffer, '0');
      }
    }
    dynstring_append_char(&buffer, '\n');
  }
  file_write_sync(g_fileStdout, dynstring_view(&buffer));
}

/**
 * Expand inherited dependencies (transitive closure).
 * https://en.wikipedia.org/wiki/Transitive_closure
 */
static void runner_dep_expand(RunnerDepMatrix* dep) {
  for (JobTaskId parent = 0; parent != dep->count; ++parent) {
    u64* parentBegin = dep->chunks + dep->strideChunks * parent;
    u64* parentEnd   = parentBegin + dep->strideChunks;

    // Iterate all the set children, includes a fast path to skip empty regions 64 bits at a time.
    for (JobTaskId child = 0; child != dep->strideBits;) {
      const u64 childChunk = parentBegin[bits_to_dwords(child)] >> bit_in_dword(child);
      if (childChunk) {
        child += intrinsic_ctz_64(childChunk); // Find the next child in the 64 bit chunk.

        // Mark children of child to be also children of parent, reason is that if child cannot
        // start yet it means that dependencies of child cannot start yet either.
        u64* childItr = dep->chunks + dep->strideChunks * child;
        NO_VECTORIZE_HINT
        for (u64* parentItr = parentBegin; parentItr != parentEnd; ++parentItr, ++childItr) {
          *parentItr |= *childItr;
        }

        ++child; // Jump to the next child.
      } else {
        child += 64 - bit_in_dword(child); // Jump to the next 64 bit aligned child.
      }
    }
  }
}

/**
 * Remove inherited dependencies (transitive reduction).
 * https://en.wikipedia.org/wiki/Transitive_reduction
 */
static void runner_dep_reduce(RunnerDepMatrix* dep) {
  for (JobTaskId parent = 0; parent != dep->count; ++parent) {
    u64* parentBegin = dep->chunks + dep->strideChunks * parent;
    u64* parentEnd   = parentBegin + dep->strideChunks;

    // Iterate all the set children, includes a fast path to skip empty regions 64 bits at a time.
    for (JobTaskId child = 0; child != dep->strideBits;) {
      const u64 childChunk = parentBegin[bits_to_dwords(child)] >> bit_in_dword(child);
      if (childChunk) {
        child += intrinsic_ctz_64(childChunk); // Find the next child in the 64 bit chunk.

        // Remove children of child as dependencies of parent, reason is that they are already
        // inherited through child.
        u64* childItr = dep->chunks + dep->strideChunks * child;
        NO_VECTORIZE_HINT
        for (u64* parentItr = parentBegin; parentItr != parentEnd; ++parentItr, ++childItr) {
          *parentItr &= ~*childItr;
        }

        ++child; // Jump to the next child.
      } else {
        child += 64 - bit_in_dword(child); // Jump to the next 64 bit aligned child.
      }
    }
  }
}

/**
 * Queue of tasks sorted by cost (highest first).
 * Executing the highest cost tasks first reduces the chance for bubbles in parallel scheduling.
 */
typedef struct {
  u32       count;
  JobTaskId tasks[128];
  u64       costs[128];
} RunnerTaskQueue;

static void runner_queue_clear(RunnerTaskQueue* q) { q->count = 0; }

static void
runner_queue_insert(RunnerTaskQueue* q, const RunnerEstimateContext* estCtx, const JobTaskId task) {
  diag_assert_msg(q->count < array_elems(q->tasks), "Task queue exhausted");

  const u64 cost = runner_estimate_task(estCtx, task);

  u32 i = 0;
  // Find the first task that is cheaper then the new task.
  for (; i != q->count && cost <= q->costs[i]; ++i)
    ;
  // If its not the last entry in the queue; move the cheaper tasks over by one.
  if (i != q->count) {
    JobTaskId* taskItr = &q->tasks[i];
    JobTaskId* taskEnd = &q->tasks[q->count];
    mem_move(mem_from_to(taskItr + 1, taskEnd + 1), mem_from_to(taskItr, taskEnd));

    u64* costItr = &q->costs[i];
    u64* costEnd = &q->costs[q->count];
    mem_move(mem_from_to(costItr + 1, costEnd + 1), mem_from_to(costItr, costEnd));
  }
  // Add it to the queue.
  q->tasks[i] = task;
  q->costs[i] = cost;
  ++q->count;
}

/**
 * Setup the parent-child relationships in graph based on the dependency matrix.
 */
static void runner_dep_apply(RunnerDepMatrix* dep, EcsRunner* runner, RunnerPlan* plan) {
  RunnerTaskQueue taskQueue;

  const RunnerEstimateContext estCtx = {.runner = runner, .plan = plan};

  for (JobTaskId parent = 0; parent != dep->count; ++parent) {
    const u64* restrict parentBegin = dep->chunks + dep->strideChunks * parent;

    runner_queue_clear(&taskQueue);

    // Collect all children, includes a fast path to skip empty regions 64 bits at a time.
    for (JobTaskId child = 0; child != dep->strideBits;) {
      const u64 childChunk = parentBegin[bits_to_dwords(child)] >> bit_in_dword(child);
      if (childChunk) {
        child += intrinsic_ctz_64(childChunk); // Find the next child in the 64 bit chunk.
        runner_queue_insert(&taskQueue, &estCtx, child);
        ++child; // Jump to the next child.
      } else {
        child += 64 - bit_in_dword(child); // Jump to the next 64 bit aligned child.
      }
    }

    // Insert the dependents into the graph.
    // Add the highest cost child first to avoid bubbles in the parallel scheduling.
    for (u32 i = 0; i != taskQueue.count; ++i) {
      jobs_graph_task_depend(plan->graph, parent, taskQueue.tasks[i]);
    }
  }
}

static void runner_system_collect(const EcsDef* def, EcsSystemDefPtr out[]) {
  const EcsSystemDef* sysDefsBegin = dynarray_begin_t(&def->systems, EcsSystemDef);

  for (EcsSystemId sysId = 0; sysId != def->systems.size; ++sysId) {
    out[sysId] = &sysDefsBegin[sysId];
  }
}

static void runner_plan_pick(EcsRunner* runner) {
  u32 bestIndex = sentinel_u32;
  u64 bestSpan  = 0;

  trace_begin("ecs_plan_pick", TraceColor_Blue);

  for (u32 i = 0; i != array_elems(runner->plans); ++i) {
    const RunnerEstimateContext ctx  = {.runner = runner, .plan = &runner->plans[i]};
    const u64                   span = runner_estimate_plan(&ctx);
    diag_assert(span < i64_max); // We store TimeDuration's as signed.

#ifdef VOLO_ECS_RUNNER_STRESS
    const bool better = rng_sample_f32(g_rng) >= 0.5f;
#else
    const bool better = span < bestSpan;
#endif
    if (sentinel_check(bestIndex) || better) {
      bestIndex = i;
      bestSpan  = span;
    }
  }

  trace_end();

  runner->planEstSpan = (TimeDuration)bestSpan;
  if (bestIndex != runner->planIndex) {
    runner->planIndexNext = bestIndex;
    ++runner->planCounter;

    log_d("Ecs new plan picked", log_param("est-span", fmt_duration(bestSpan)));
  }
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
  // TODO: Consider using a stable sorting algorithm to preserve more randomness from the shuffle.
  sort_quicksort_t(systems, systems + systemCount, EcsSystemDefPtr, compare_system_entry);

  trace_end();
  trace_begin("ecs_plan_build", TraceColor_Blue);

  // Insert the systems into the job-graph.
  jobs_graph_clear(plan->graph);

  /**
   * Build up a dependency matrix and later insert the dependencies in the graph.
   * Reason is its easier to optimize the transitive reduction step in matrix form as it is to
   * optimize the 'jobs_graph_reduce_dependencies()' graph utility.
   */
  const u32       depStrideBits   = bits_align_32(runner->taskCount, 64);
  const u32       depStrideChunks = bits_to_dwords(depStrideBits);
  RunnerDepMatrix depMatrix       = {
            .chunks       = mem_stack(runner->taskCount * depStrideChunks * sizeof(u64)).ptr,
            .strideBits   = depStrideBits,
            .strideChunks = depStrideChunks,
            .count        = runner->taskCount,
  };
  runner_dep_clear(&depMatrix);

  // Insert meta tasks.
  plan->metaTasks[EcsRunnerMetaTask_Replan] = runner_insert_replan(runner, planIndex).begin;
  plan->metaTasks[EcsRunnerMetaTask_Flush]  = runner_insert_flush(runner, planIndex).begin;

  // Insert system tasks.
  for (EcsSystemDefPtr* sysDef = systems; sysDef != systems + systemCount; ++sysDef) {
    const EcsSystemId sysId    = ecs_def_system_id(def, *sysDef);
    const EcsTaskSet  sysTasks = runner_insert_system(runner, planIndex, sysId, *sysDef);
    plan->systemTasks[sysId]   = sysTasks;

    // Insert a flush dependency (so flush only happens when all systems are done).
    runner_dep_add_to_many(&depMatrix, sysTasks, plan->metaTasks[EcsRunnerMetaTask_Flush]);

    // Insert required dependencies on the earlier systems.
    for (EcsSystemDefPtr* earlierSysDef = systems; earlierSysDef != sysDef; ++earlierSysDef) {
      const EcsSystemId earlierSysId = ecs_def_system_id(def, *earlierSysDef);
      if (runner_conflict_query(runner->sysConflicts, sysId, earlierSysId)) {
        runner_dep_add_many(&depMatrix, plan->systemTasks[earlierSysId], sysTasks);
      }
    }
  }

  trace_end();
  trace_begin("ecs_plan_finalize", TraceColor_Blue);

  // Transitively reduce the matrix and insert the dependencies into the graph.
  runner_dep_expand(&depMatrix);
  runner_dep_reduce(&depMatrix);
  runner_dep_apply(&depMatrix, runner, plan);

#if defined(VOLO_ECS_RUNNER_VERBOSE)
  runner_dep_dump(&depMatrix, plan->graph);
#endif

#if defined(VOLO_ECS_RUNNER_VALIDATION)
  diag_assert(jobs_graph_validate(plan->graph));
  diag_assert(jobs_graph_reduce_dependencies(plan->graph) == 0); // Test for redundant dependencies.
#endif

  trace_end();
}

EcsRunner* ecs_runner_create(Allocator* alloc, EcsWorld* world, const EcsRunnerFlags flags) {
  const EcsDef* def         = ecs_world_def(world);
  const u32     systemCount = (u32)def->systems.size;

  EcsRunner* runner = alloc_alloc_t(alloc, EcsRunner);

  *runner = (EcsRunner){
      .alloc        = alloc,
      .world        = world,
      .flags        = flags,
      .taskCount    = runner_task_count_total(def),
      .sysConflicts = runner_conflict_matrix_create(world, alloc),
  };

  if (systemCount) {
    runner->sysStats = alloc_array_t(alloc, RunnerSystemStats, systemCount);
    mem_set(mem_create(runner->sysStats, sizeof(RunnerSystemStats) * systemCount), 0);
  }

  array_for_t(runner->plans, RunnerPlan, plan) {
    plan->graph       = jobs_graph_create(alloc, string_lit("ecs_runner"), runner->taskCount);
    plan->systemTasks = systemCount ? alloc_array_t(alloc, EcsTaskSet, systemCount) : null;
  }

  runner_plan_formulate(runner, runner->planIndex, false /* shuffle */);

  // Allocate the runtime memory required to run the graph (reused for every run).
  // NOTE: +64 for bump allocator overhead.
  const JobGraph* graph = runner->plans[runner->planIndex].graph;
  diag_assert(jobs_graph_task_count(graph) == runner->taskCount);
  const usize jobMemSize = jobs_scheduler_mem_size(graph) + 64;
  runner->jobMem         = alloc_alloc(alloc, jobMemSize, jobs_scheduler_mem_align(graph));

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
  if (mem_valid(runner->sysConflicts)) {
    alloc_free(runner->alloc, runner->sysConflicts);
  }
  if (runner->sysStats) {
    alloc_free_array_t(runner->alloc, runner->sysStats, systemCount);
  }
  alloc_free(runner->alloc, runner->jobMem);
  alloc_free_t(runner->alloc, runner);
}

EcsRunnerStats ecs_runner_stats_query(const EcsRunner* runner) {
  return (EcsRunnerStats){
      .flushDurLast = runner->metaStats[EcsRunnerMetaTask_Flush].durLast,
      .flushDurAvg  = runner->metaStats[EcsRunnerMetaTask_Flush].durAvg,
      .planCounter  = runner->planCounter,
      .planEstSpan  = runner->planEstSpan,
  };
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
  return runner->sysStats[systemId].totalDurAvg;
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
