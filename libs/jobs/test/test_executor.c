#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_thread.h"
#include "jobs_graph.h"
#include "jobs_scheduler.h"

typedef struct {
  i64* counter;
} TestExecutorCounterData;

typedef struct {
  i64*  values;
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

static void test_task_counter_to_42(void* ctx) {
  TestExecutorCounterData* data = ctx;
  *data->counter                = 42;
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
    i64   data[1024 * 8];
    usize dataCount = array_elems(data);
    i64   sum       = 0;
    for (i64 i = 0; i != (i64)dataCount; ++i) {
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

  it("suports one-to-many task dependencies") {
    const usize tasks = 128;

    i64 data[128 + 1];
    mem_set(array_mem(data), 0);

    JobGraph*       graph    = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 1);
    const JobTaskId initTask = jobs_graph_add_task(
        graph,
        string_lit("Init"),
        test_task_counter_to_42,
        mem_struct(TestExecutorCounterData, .counter = &data[0]));

    for (usize i = 0; i != tasks; ++i) {
      const JobTaskId task = jobs_graph_add_task(
          graph,
          string_lit("SetVal"),
          test_task_sum,
          mem_struct(TestExecutorSumData, .values = data, .idxA = i + 1, .idxB = 0));
      jobs_graph_task_depend(graph, initTask, task);
    }

    jobs_scheduler_wait_help(jobs_scheduler_run(graph));
    array_for_t(data, i64, val, { check_eq_int(*val, 42); });

    jobs_graph_destroy(graph);
  }
}
