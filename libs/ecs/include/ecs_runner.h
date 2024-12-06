#pragma once
#include "core.h"
#include "ecs.h"

// Forward declare from 'jobs_scheduler.h'.
typedef u64 JobId;

// Forward declare from 'jobs_graph.h'.
typedef struct sJobGraph JobGraph;
typedef u16              JobTaskId;

typedef struct sEcsRunner EcsRunner;

typedef struct {
  JobTaskId begin, end; // NOTE: End is exclusive.
} EcsTaskSet;

typedef enum {
  EcsRunnerFlags_None   = 0,
  EcsRunnerFlags_Replan = 1 << 0, // Automatically compute new plans when running.

  EcsRunnerFlags_Count = 1,
} EcsRunnerFlags;

/**
 * True while the current thread is running an ecs system.
 */
extern THREAD_LOCAL bool        g_ecsRunningSystem;
extern THREAD_LOCAL EcsSystemId g_ecsRunningSystemId;
extern THREAD_LOCAL const EcsRunner* g_ecsRunningRunner;

/**
 * Create a new Ecs runner for the given world.
 * NOTE: The world must remain valid while this runner exists.
 * Destroy using 'ecs_runner_destroy()'.
 */
EcsRunner* ecs_runner_create(Allocator*, EcsWorld*, EcsRunnerFlags flags);

/**
 * Destroy a Ecs runner.
 *
 * Pre-condition: !ecs_running().
 */
void ecs_runner_destroy(EcsRunner*);

typedef struct {
  TimeDuration flushDurLast, flushDurAvg;
  u64          planCounter;
  TimeDuration planEstSpan; // Estimated duration of the longest span through the graph.
} EcsRunnerStats;

/**
 * Query statistics for the given runner.
 */
EcsRunnerStats  ecs_runner_stats_query(const EcsRunner*);
const JobGraph* ecs_runner_graph(const EcsRunner*);
EcsTaskSet      ecs_runner_task_set(const EcsRunner*, EcsSystemId);
TimeDuration    ecs_runner_duration_avg(const EcsRunner*, EcsSystemId);

/**
 * Check if the given runner is currently running.
 */
bool ecs_running(const EcsRunner*);

/**
 * Start executing this runner asynchronously.
 * Use 'jobs_scheduler_is_finished()' to check if the job is finished.
 * Use 'jobs_scheduler_wait()' to wait for the job to be finished.
 * Use 'jobs_scheduler_wait_help()' to help finishing the job.
 *
 * Pre-condition: !ecs_running().
 */
JobId ecs_run_async(EcsRunner*);

/**
 * Synchronously execute this runner
 *
 * Pre-condition: !ecs_running().
 */
void ecs_run_sync(EcsRunner*);
