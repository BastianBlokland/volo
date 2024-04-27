#pragma once
#include "core_annotation.h"
#include "core_types.h"

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
 * True if the current thread is currently performing work for the job system.
 */
extern THREAD_LOCAL bool g_jobsIsWorking;

/**
 * Id of the currently executing task.
 * NOTE: Only valid if 'g_jobsIsWorking' is true.
 */
extern THREAD_LOCAL JobTaskId g_jobsTaskId;
