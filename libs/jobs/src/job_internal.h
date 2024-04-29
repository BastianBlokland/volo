#pragma once
#include "core_annotation.h"
#include "jobs_graph.h"

// Padded to 64 bytes to avoid false-sharing of cachelines.
#define job_align 64

typedef u64 JobId;

/**
 * Per task runtime data.
 */
typedef struct {
  ALIGNAS(job_align) i64 dependencies; // Remaining dependencies (parent tasks).
  ALIGNAS(16) u8 scratchpad[32];       // For users to use as a per-task temporary memory.
} JobTaskData;

ASSERT(sizeof(JobTaskData) == job_align, "Invalid JobTaskData size");

/**
 * Per job runtime data.
 */
typedef struct {
  ALIGNAS(job_align) JobId id;
  const JobGraph* graph;
  Allocator*      jobAlloc;
  i64             dependencies; // Remaining dependencies (leaf tasks).
  JobTaskData     taskData[];   // Allocated after this struct.
} Job;

ASSERT(sizeof(Job) == job_align, "Invalid Job size");

usize job_mem_req_size(const JobGraph*);
usize job_mem_req_align(const JobGraph*);

Job* job_create(Allocator*, const JobId id, const JobGraph*);
void job_destroy(Job*);
