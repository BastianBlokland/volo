#include "core_alloc.h"
#include "core_diag.h"
#include "jobs_scheduler.h"

static void test_task_nop(void* ctx) { (void)ctx; }

static void test_scheduler_single_task_job_finishes() {
  Allocator* alloc = alloc_bump_create_stack(1024);

  JobGraph* jobGraph = jobs_graph_create(alloc, string_lit("TestJob"), 1);
  jobs_graph_add_task(jobGraph, string_lit("TestTask"), test_task_nop, mem_empty);

  JobId id = jobs_scheduler_run(jobGraph);
  jobs_scheduler_wait(id);
  diag_assert(jobs_scheduler_is_finished(id));

  jobs_graph_destroy(jobGraph);
}

static void test_scheduler_single_graph_can_be_run_multiple_times() {
  static const usize numRuns = 128;

  Allocator* alloc = alloc_bump_create_stack(2048);

  JobGraph* jobGraph = jobs_graph_create(alloc, string_lit("TestJob"), 1);
  jobs_graph_add_task(jobGraph, string_lit("TestTask"), test_task_nop, mem_empty);

  DynArray jobIds = dynarray_create_t(alloc, JobId, numRuns);

  // Start the graph multiple times.
  for (usize i = 0; i != numRuns; ++i) {
    *dynarray_push_t(&jobIds, JobId) = jobs_scheduler_run(jobGraph);
  }

  // Wait for all jobs to finish.
  dynarray_for_t(&jobIds, JobId, id, {
    jobs_scheduler_wait(*id);
    diag_assert(jobs_scheduler_is_finished(*id));
  });

  dynarray_destroy(&jobIds);
  jobs_graph_destroy(jobGraph);
}

void test_scheduler() {
  test_scheduler_single_task_job_finishes();
  test_scheduler_single_graph_can_be_run_multiple_times();
}
