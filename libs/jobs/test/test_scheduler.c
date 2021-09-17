#include "check_spec.h"
#include "core_alloc.h"
#include "jobs_scheduler.h"

static void test_task_nop(void* ctx) { (void)ctx; }

spec(scheduler) {

  it("can run a single-task job-graph") {
    JobGraph* jobGraph = jobs_graph_create(g_alloc_scratch, string_lit("TestJob"), 1);
    jobs_graph_add_task(jobGraph, string_lit("TestTask"), test_task_nop, mem_empty);

    JobId id = jobs_scheduler_run(jobGraph);
    jobs_scheduler_wait_help(id);
    check(jobs_scheduler_is_finished(id));

    jobs_graph_destroy(jobGraph);
  }

  it("can run a job-graph multiple times") {
    static const usize numRuns = 128;

    JobGraph* jobGraph = jobs_graph_create(g_alloc_scratch, string_lit("TestJob"), 1);
    jobs_graph_add_task(jobGraph, string_lit("TestTask"), test_task_nop, mem_empty);

    DynArray jobIds = dynarray_create_t(g_alloc_scratch, JobId, numRuns);

    // Start the graph multiple times.
    for (usize i = 0; i != numRuns; ++i) {
      *dynarray_push_t(&jobIds, JobId) = jobs_scheduler_run(jobGraph);
    }

    // Wait for all jobs to finish.
    dynarray_for_t(&jobIds, JobId, id, {
      jobs_scheduler_wait_help(*id);
      check(jobs_scheduler_is_finished(*id));
    });

    dynarray_destroy(&jobIds);
    jobs_graph_destroy(jobGraph);
  }
}
