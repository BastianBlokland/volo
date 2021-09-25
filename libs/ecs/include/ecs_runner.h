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
 */
void ecs_runner_destroy(EcsRunner*);

/**
 * Start executing this runner asynchronously.
 * Use 'jobs_scheduler_is_finished()' to check if the job is finished.
 * Use 'jobs_scheduler_wait()' to wait for the job to be finished.
 * Use 'jobs_scheduler_wait_help()' to help finishing the job.
 */
JobId ecs_run_async(const EcsRunner*);

/**
 * Synchronously execute this runner.
 */
void ecs_run_sync(const EcsRunner*);
