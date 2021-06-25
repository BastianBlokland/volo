#pragma once
#include "jobs_graph.h"

typedef u64 JobId;

/**
 * Per task runtime data.
 * TODO: Consider padding this to 64 bits to avoid false-sharing of cachelines. At the moment we
 * induce full memory barriers when updating these atomically so 'false-sharing' is not that big of
 * a deal yet, but as soon we improve that the sharing of cachelines will likely start to hurt
 * performance.
 */
typedef struct {
  i64 dependencies; // Remaining dependencies (parent tasks).
} JobTaskData;

/**
 * Per job runtime data.
 */
typedef struct {
  JobId           id;
  const JobGraph* graph;
  i64             dependencies; // Remaining dependencies (leaf tasks).
  JobTaskData     taskData[];   // Allocated after this struct.
} Job;

Job* job_create(Allocator*, const JobId id, const JobGraph*);
void job_destroy(Allocator*, Job*);
