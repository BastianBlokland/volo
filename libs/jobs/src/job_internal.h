#pragma once
#include "core_annotation.h"
#include "jobs_graph.h"

// Padded to 64 bytes to avoid false-sharing of cachelines.
#define job_size 64

typedef u64 JobId;

/**
 * Per task runtime data.
 */
typedef struct {
  ALIGNAS(job_size) i64 dependencies; // Remaining dependencies (parent tasks).
} JobTaskData;

ASSERT(sizeof(JobTaskData) == job_size, "Invalid JobTaskData size");

/**
 * Per job runtime data.
 */
typedef struct {
  ALIGNAS(job_size) JobId id;
  const JobGraph* graph;
  i64             dependencies; // Remaining dependencies (leaf tasks).
  JobTaskData     taskData[];   // Allocated after this struct.
} Job;

ASSERT(sizeof(Job) == job_size, "Invalid Job size");

Job* job_create(Allocator*, const JobId id, const JobGraph*);
void job_destroy(Allocator*, Job*);
