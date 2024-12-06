#pragma once
#include "core.h"

/**
 * Forward header for the jobs library.
 */

typedef enum eJobTaskFlags JobTaskFlags;
typedef struct sJobGraph   JobGraph;
typedef struct sJobsConfig JobsConfig;
typedef u16                JobTaskId;
typedef u16                JobTaskId;
typedef u16                JobWorkerId;
typedef u64                JobId;

extern u16 g_jobsWorkerCount;

extern THREAD_LOCAL bool        g_jobsIsWorker;
extern THREAD_LOCAL JobTaskId   g_jobsTaskId;
extern THREAD_LOCAL JobWorkerId g_jobsWorkerId;
