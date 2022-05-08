#pragma once
#include "core_annotation.h"
#include "core_types.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'ecs_world.h'.
typedef struct sEcsWorld EcsWorld;

// Forward declare from 'ecs_module.h'.
typedef u16 EcsSystemId;

// Forward declare from 'jobs_scheduler.h'.
typedef u64 JobId;

// Forward declare from 'jobs_graph.h'.
typedef struct sJobGraph JobGraph;
typedef u32              JobTaskId;

typedef struct sEcsRunner EcsRunner;

typedef enum {
  EcsRunnerFlags_None         = 0,
  EcsRunnerFlags_DumpGraphDot = 1 << 0,

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

/**
 * Get the JobGraph created by this runner for debugging purposes.
 */
const JobGraph* ecs_runner_graph(const EcsRunner*);
JobTaskId       ecs_runner_graph_task(const EcsRunner*, EcsSystemId);

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
