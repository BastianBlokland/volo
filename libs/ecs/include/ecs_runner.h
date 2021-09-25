#pragma once
#include "core_annotation.h"
#include "core_types.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'ecs_world.h'.
typedef struct sEcsWorld EcsWorld;

// Forward declare from 'jos_scheduler.h'.
typedef u64 JobId;

/**
 * Ecs runner.
 * Responsible for executing ecs systems.
 */
typedef struct sEcsRunner EcsRunner;

/**
 * True while the current thread is running an ecs system.
 */
extern THREAD_LOCAL bool g_ecsRunningSystem;

/**
 * Create a new Ecs runner for the given world.
 * Note: The world must remain valid while this runner exists.
 * Destroy using 'ecs_runner_destroy()'.
 */
EcsRunner* ecs_runner_create(Allocator*, EcsWorld*);

/**
 * Destroy a Ecs runner.
 *
 * Pre-condition: !ecs_running().
 */
void ecs_runner_destroy(EcsRunner*);

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
