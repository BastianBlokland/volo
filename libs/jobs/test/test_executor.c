#include "core_alloc.h"
#include "core_array.h"
#include "core_thread.h"

#include "jobs_graph.h"
#include "jobs_scheduler.h"

#include "check_spec.h"

typedef struct {
  i64* counter;
} TestExecutorCounterData;

typedef struct {
  i32*  values;
  usize idxA, idxB;
} TestExecutorSumData;

static void test_task_increment_counter(void* ctx) {
  TestExecutorCounterData* data = ctx;
  ++*data->counter;
}

static void test_task_decrement_counter(void* ctx) {
  TestExecutorCounterData* data = ctx;
  --*data->counter;
}

static void test_task_increment_counter_atomic(void* ctx) {
  TestExecutorCounterData* data = ctx;
  thread_atomic_add_i64(data->counter, 1);
}

static void test_task_sum(void* ctx) {
  TestExecutorSumData* data = ctx;
  data->values[data->idxA] += data->values[data->idxB];
}

spec(executor) {

  it("can execute a linear chain of tasks") {
    static const usize numTasks = 1000;

    JobGraph* jobGraph = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 1);

    i64 counter = 0;
    for (usize i = 0; i != numTasks; ++i) {
      jobs_graph_add_task(
          jobGraph,
          string_lit("Increment"),
          test_task_increment_counter,
          mem_struct(TestExecutorCounterData, .counter = &counter));
      if (i) {
        jobs_graph_task_depend(jobGraph, (JobTaskId)(i - 1), (JobTaskId)i);
      }
    }

    jobs_scheduler_wait_help(jobs_scheduler_run(jobGraph));
    check_eq_int((usize)counter, numTasks);

    jobs_scheduler_wait_help(jobs_scheduler_run(jobGraph));
    check_eq_int((usize)counter, numTasks * 2);

    jobs_graph_destroy(jobGraph);
  }

  it("executes a linear chain of tasks in the correct order") {
    static const usize numTasks = 1000;

    JobGraph* jobGraph = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 1);

    i64 counter = 0;
    for (usize i = 0; i != numTasks; ++i) {
      if (i % 2) {
        jobs_graph_add_task(
            jobGraph,
            string_lit("Decrement"),
            test_task_decrement_counter,
            mem_struct(TestExecutorCounterData, .counter = &counter));
      } else {
        jobs_graph_add_task(
            jobGraph,
            string_lit("Increment"),
            test_task_increment_counter,
            mem_struct(TestExecutorCounterData, .counter = &counter));
      }
      if (i) {
        jobs_graph_task_depend(jobGraph, (JobTaskId)i - 1, (JobTaskId)i);
      }
    }

    jobs_scheduler_wait_help(jobs_scheduler_run(jobGraph));
    check_eq_int((usize)counter, 0);

    jobs_scheduler_wait_help(jobs_scheduler_run(jobGraph));
    check_eq_int((usize)counter, 0);

    jobs_graph_destroy(jobGraph);
  }

  it("can execute a set of parallel tasks") {
    static const usize numTasks = 1000;

    JobGraph* jobGraph = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 1);

    i64 counter = 0;
    for (usize i = 0; i != numTasks; ++i) {
      jobs_graph_add_task(
          jobGraph,
          string_lit("Increment"),
          test_task_increment_counter_atomic,
          mem_struct(TestExecutorCounterData, .counter = &counter));
    }

    jobs_scheduler_wait_help(jobs_scheduler_run(jobGraph));
    check_eq_int((usize)counter, numTasks);

    jobs_scheduler_wait_help(jobs_scheduler_run(jobGraph));
    check_eq_int((usize)counter, numTasks * 2);

    jobs_graph_destroy(jobGraph);
  }

  it("can compute a parallel sum of integers") {
    i32   data[1024 * 8];
    usize dataCount = array_elems(data);
    i32   sum       = 0;
    for (i32 i = 0; i != (i32)dataCount; ++i) {
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

    jobs_scheduler_wait_help(jobs_scheduler_run(graph));
    check_eq_int(data[0], sum);

    dynarray_destroy(&dependencies);
    jobs_graph_destroy(graph);
  }
}
