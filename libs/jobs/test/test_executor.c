#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "jobs_graph.h"
#include "jobs_scheduler.h"

typedef struct {
  i64* counter;
} TestExecutorCounterData;

typedef struct {
  i32*  values;
  usize idxA, idxB;
} TestExecutorSumData;

static void test_task_increment_counter(void* ctx) {
  TestExecutorCounterData* data = ctx;
  thread_atomic_add_i64(data->counter, 1);
}

static void test_task_sum(void* ctx) {
  TestExecutorSumData* data = ctx;
  data->values[data->idxA] += data->values[data->idxB];
}

static void test_executor_linear_chain_of_tasks() {
  static const usize numTasks = 5000;

  JobGraph* jobGraph = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 1);

  i64       counter  = 0;
  JobTaskId prevTask = sentinel_u32;
  for (usize i = 0; i != numTasks; ++i) {
    JobTaskId id = jobs_graph_add_task(
        jobGraph,
        string_lit("Increment"),
        test_task_increment_counter,
        mem_struct(TestExecutorCounterData, .counter = &counter));

    if (!sentinel_check(prevTask)) {
      jobs_graph_task_depend(jobGraph, prevTask, id);
    }
    prevTask = id;
  }

  jobs_scheduler_wait(jobs_scheduler_run(jobGraph));

  diag_assert(counter == numTasks);

  jobs_graph_destroy(jobGraph);
}

static void test_executor_parallel_chain_of_tasks() {
  static const usize numTasks = 8000;

  JobGraph* jobGraph = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 1);

  i64 counter = 0;
  for (usize i = 0; i != numTasks; ++i) {
    jobs_graph_add_task(
        jobGraph,
        string_lit("Increment"),
        test_task_increment_counter,
        mem_struct(TestExecutorCounterData, .counter = &counter));
  }

  jobs_scheduler_wait(jobs_scheduler_run(jobGraph));
  diag_assert(counter == numTasks);

  jobs_graph_destroy(jobGraph);
}

static void test_executor_parallel_sum() {
  static const usize dataCount = 1024 * 16;
  i32                data[dataCount];
  i32                sum = 0;
  for (usize i = 0; i != dataCount; ++i) {
    sum += data[i] = i;
  }

  JobGraph* graph = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 1);

  DynArray dependencies = dynarray_create_t(g_alloc_heap, JobTaskId, dataCount / 2);
  dynarray_resize(&dependencies, dataCount / 2);

  bool rootLayer = true;
  for (usize halfSize = dataCount / 2; halfSize; halfSize /= 2) {
    for (usize i = 0; i != halfSize; ++i) {
      const JobTaskId id = jobs_graph_add_task(
          graph,
          string_lit("Sum"),
          test_task_sum,
          mem_struct(TestExecutorSumData, .values = data, .idxA = i, .idxB = halfSize + i));
      if (!rootLayer) {
        jobs_graph_task_depend(graph, *dynarray_at_t(&dependencies, i, JobTaskId), id);
        jobs_graph_task_depend(graph, *dynarray_at_t(&dependencies, halfSize + i, JobTaskId), id);
      }
      *dynarray_at_t(&dependencies, i, JobTaskId) = id;
    }
    rootLayer = false;
  }

  jobs_scheduler_wait(jobs_scheduler_run(graph));
  diag_assert(data[0] == sum);

  dynarray_destroy(&dependencies);
  jobs_graph_destroy(graph);
}

void test_executor() {
  test_executor_linear_chain_of_tasks();
  test_executor_parallel_chain_of_tasks();
  test_executor_parallel_sum();
}
