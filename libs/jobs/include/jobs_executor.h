#pragma once
#include "core.h"
#include "core_memory.h"

// Forward declare from 'jobs_graph.h'.
typedef u16 JobTaskId;

/**
 * Identifier for a worker in the job system.
 * NOTE: The main-thread is also considered a worker.
 */
typedef u16 JobWorkerId;

/**
 * Number of workers.
 * NOTE: The main-thread is also considered a worker.
 */
extern u16 g_jobsWorkerCount;

/**
 * JobWorkerId of the current thread.
 * Pre-condition: g_jobsIsWorker == true
 */
extern THREAD_LOCAL JobWorkerId g_jobsWorkerId;

/**
 * True if the current thread is a worker.
 * NOTE: The main-thread is also considered a worker.
 */
extern THREAD_LOCAL bool g_jobsIsWorker;

/**
 * Id of the currently executing task.
 * NOTE: Only valid if 'jobs_is_working()' is true.
 */
extern THREAD_LOCAL JobTaskId g_jobsTaskId;

/**
 * True if the current thread is currently performing work for the job system.
 */
bool jobs_is_working(void);

/**
 * Retrieve the scratchpad for the given task in the current job.
 * NOTE: Memory is guaranteed to be at least 32 bytes and 16 byte aligned.
 * Pre-condition: jobs_is_working() == true
 */
Mem jobs_scratchpad(JobTaskId);
