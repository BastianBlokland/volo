#include "core_alloc.h"
#include "core_diag.h"
#include "jobs_graph.h"

static void test_jobs_graph_name_can_be_retrieved() {
  Allocator* alloc = alloc_bump_create_stack(256);
  JobGraph*  job   = jobs_graph_create(alloc, string_lit("TestJob"), 0);

  diag_assert(string_eq(jobs_graph_name(job), string_lit("TestJob")));

  jobs_graph_destroy(job);
}

static void test_jobs_graph_task_name_can_be_retrieved() {
  Allocator* alloc = alloc_bump_create_stack(512);
  JobGraph*  job   = jobs_graph_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId taskA = jobs_graph_add_task(job, string_lit("TestTaskA"), null, null);
  const JobTaskId taskB = jobs_graph_add_task(job, string_lit("TestTaskB"), null, null);

  diag_assert(jobs_graph_task_count(job) == 2);
  diag_assert(string_eq(jobs_graph_task_name(job, taskA), string_lit("TestTaskA")));
  diag_assert(string_eq(jobs_graph_task_name(job, taskB), string_lit("TestTaskB")));

  jobs_graph_destroy(job);
}

static void test_jobs_graph_many_to_one_dependency() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobGraph*  job   = jobs_graph_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, null);

  diag_assert(jobs_graph_task_count(job) == 4);

  // Setup D to depend on A, B and C.
  jobs_graph_task_depend(job, a, d);
  jobs_graph_task_depend(job, b, d);
  jobs_graph_task_depend(job, c, d);

  diag_assert(jobs_graph_task_span(job) == 2);
  diag_assert(jobs_graph_validate(job));
  diag_assert(jobs_graph_task_root_count(job) == 3);
  diag_assert(jobs_graph_task_leaf_count(job) == 1);

  // Meaning only D has a parent.
  diag_assert(jobs_graph_task_has_parent(job, d));
  diag_assert(!jobs_graph_task_has_parent(job, a));
  diag_assert(!jobs_graph_task_has_parent(job, b));
  diag_assert(!jobs_graph_task_has_parent(job, c));

  // And A, B, C have a child.
  diag_assert(jobs_graph_task_has_child(job, a));
  diag_assert(jobs_graph_task_has_child(job, b));
  diag_assert(jobs_graph_task_has_child(job, c));
  diag_assert(!jobs_graph_task_has_child(job, d));

  diag_assert(jobs_graph_task_child_begin(job, a).task == d);
  diag_assert(jobs_graph_task_child_begin(job, b).task == d);
  diag_assert(jobs_graph_task_child_begin(job, c).task == d);
  diag_assert(sentinel_check(jobs_graph_task_child_begin(job, d).task));

  jobs_graph_destroy(job);
}

static void test_jobs_graph_one_to_many_dependency() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobGraph*  job   = jobs_graph_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, null);

  diag_assert(jobs_graph_task_count(job) == 4);

  // Setup B, C, D to depend on A.
  jobs_graph_task_depend(job, a, b);
  jobs_graph_task_depend(job, a, c);
  jobs_graph_task_depend(job, a, d);

  diag_assert(jobs_graph_validate(job));
  diag_assert(jobs_graph_task_span(job) == 2);
  diag_assert(jobs_graph_task_root_count(job) == 1);
  diag_assert(jobs_graph_task_leaf_count(job) == 3);

  // Meaning B, C, D have a parent.
  diag_assert(!jobs_graph_task_has_parent(job, a));
  diag_assert(jobs_graph_task_has_parent(job, b));
  diag_assert(jobs_graph_task_has_parent(job, c));
  diag_assert(jobs_graph_task_has_parent(job, d));

  // And only A has a child.
  diag_assert(jobs_graph_task_has_child(job, a));
  diag_assert(!jobs_graph_task_has_child(job, b));
  diag_assert(!jobs_graph_task_has_child(job, c));
  diag_assert(!jobs_graph_task_has_child(job, d));

  // Verify A has B, C, D as children.
  // TODO: Should we guarantee the order of dependencies like this? The current implementation does
  // keep the order but there is no real reason we need to.
  JobTaskChildItr itr = jobs_graph_task_child_begin(job, a);
  diag_assert(itr.task == b);
  itr = jobs_graph_task_child_next(job, itr);
  diag_assert(itr.task == c);
  itr = jobs_graph_task_child_next(job, itr);
  diag_assert(itr.task == d);
  itr = jobs_graph_task_child_next(job, itr);
  diag_assert(sentinel_check(itr.task));

  jobs_graph_destroy(job);
}

static void test_jobs_graph_validate_fails_if_cycle() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobGraph*  job   = jobs_graph_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, null);

  // Setup cycle between A and B.
  jobs_graph_task_depend(job, a, b);
  jobs_graph_task_depend(job, b, a);

  diag_assert(!jobs_graph_validate(job));

  jobs_graph_destroy(job);
}

static void test_jobs_graph_validate_fails_if_indirect_cycle() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobGraph*  job   = jobs_graph_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, null);
  const JobTaskId e = jobs_graph_add_task(job, string_lit("E"), null, null);
  const JobTaskId f = jobs_graph_add_task(job, string_lit("F"), null, null);
  const JobTaskId g = jobs_graph_add_task(job, string_lit("G"), null, null);

  jobs_graph_task_depend(job, a, b);
  jobs_graph_task_depend(job, a, c);
  jobs_graph_task_depend(job, b, d);
  jobs_graph_task_depend(job, c, d);
  jobs_graph_task_depend(job, d, e);
  jobs_graph_task_depend(job, f, e);
  jobs_graph_task_depend(job, g, d);
  jobs_graph_task_depend(job, e, c);

  diag_assert(!jobs_graph_validate(job));

  jobs_graph_destroy(job);
}

static void test_jobs_graph_task_span_serial_chain() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobGraph*  job   = jobs_graph_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, null);
  const JobTaskId e = jobs_graph_add_task(job, string_lit("E"), null, null);
  const JobTaskId f = jobs_graph_add_task(job, string_lit("F"), null, null);
  const JobTaskId g = jobs_graph_add_task(job, string_lit("G"), null, null);

  jobs_graph_task_depend(job, a, b);
  jobs_graph_task_depend(job, b, c);
  jobs_graph_task_depend(job, c, d);
  jobs_graph_task_depend(job, d, e);
  jobs_graph_task_depend(job, e, f);
  jobs_graph_task_depend(job, f, g);

  diag_assert(jobs_graph_validate(job));
  diag_assert(jobs_graph_task_span(job) == 7);
  diag_assert(jobs_graph_task_root_count(job) == 1);
  diag_assert(jobs_graph_task_leaf_count(job) == 1);

  jobs_graph_destroy(job);
}

static void test_jobs_graph_task_span_parallel_chain() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobGraph*  job   = jobs_graph_create(alloc, string_lit("TestJob"), 2);

  jobs_graph_add_task(job, string_lit("A"), null, null);
  jobs_graph_add_task(job, string_lit("B"), null, null);
  jobs_graph_add_task(job, string_lit("C"), null, null);
  jobs_graph_add_task(job, string_lit("D"), null, null);
  jobs_graph_add_task(job, string_lit("E"), null, null);
  jobs_graph_add_task(job, string_lit("F"), null, null);
  jobs_graph_add_task(job, string_lit("G"), null, null);

  diag_assert(jobs_graph_validate(job));
  diag_assert(jobs_graph_task_span(job) == 1);
  diag_assert(jobs_graph_task_root_count(job) == 7);
  diag_assert(jobs_graph_task_leaf_count(job) == 7);

  jobs_graph_destroy(job);
}

static void test_jobs_graph_task_span_complex_chain() {
  JobGraph* job = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 2);

  const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, null);
  const JobTaskId e = jobs_graph_add_task(job, string_lit("E"), null, null);
  const JobTaskId f = jobs_graph_add_task(job, string_lit("F"), null, null);
  const JobTaskId g = jobs_graph_add_task(job, string_lit("G"), null, null);
  const JobTaskId h = jobs_graph_add_task(job, string_lit("H"), null, null);
  const JobTaskId i = jobs_graph_add_task(job, string_lit("I"), null, null);
  const JobTaskId j = jobs_graph_add_task(job, string_lit("J"), null, null);
  const JobTaskId k = jobs_graph_add_task(job, string_lit("K"), null, null);
  const JobTaskId l = jobs_graph_add_task(job, string_lit("L"), null, null);
  const JobTaskId m = jobs_graph_add_task(job, string_lit("M"), null, null);
  const JobTaskId n = jobs_graph_add_task(job, string_lit("N"), null, null);
  const JobTaskId o = jobs_graph_add_task(job, string_lit("O"), null, null);
  const JobTaskId p = jobs_graph_add_task(job, string_lit("P"), null, null);
  const JobTaskId q = jobs_graph_add_task(job, string_lit("Q"), null, null);
  const JobTaskId r = jobs_graph_add_task(job, string_lit("R"), null, null);

  jobs_graph_task_depend(job, a, b);
  jobs_graph_task_depend(job, b, c);
  jobs_graph_task_depend(job, b, n);
  jobs_graph_task_depend(job, c, d);
  jobs_graph_task_depend(job, c, e);
  jobs_graph_task_depend(job, e, f);
  jobs_graph_task_depend(job, e, g);
  jobs_graph_task_depend(job, f, h);
  jobs_graph_task_depend(job, g, i);
  jobs_graph_task_depend(job, d, j);
  jobs_graph_task_depend(job, j, k);
  jobs_graph_task_depend(job, h, k);
  jobs_graph_task_depend(job, i, k);
  jobs_graph_task_depend(job, k, l);
  jobs_graph_task_depend(job, l, m);
  jobs_graph_task_depend(job, n, q);
  jobs_graph_task_depend(job, n, r);
  jobs_graph_task_depend(job, q, o);
  jobs_graph_task_depend(job, r, p);
  jobs_graph_task_depend(job, o, m);
  jobs_graph_task_depend(job, p, m);

  diag_assert(jobs_graph_validate(job));
  diag_assert(jobs_graph_task_span(job) == 9);
  diag_assert(jobs_graph_task_parallelism(job) == 2.0f);
  diag_assert(jobs_graph_task_root_count(job) == 1);
  diag_assert(jobs_graph_task_leaf_count(job) == 1);

  jobs_graph_destroy(job);
}

void test_graph() {
  test_jobs_graph_name_can_be_retrieved();
  test_jobs_graph_task_name_can_be_retrieved();
  test_jobs_graph_many_to_one_dependency();
  test_jobs_graph_one_to_many_dependency();
  test_jobs_graph_validate_fails_if_cycle();
  test_jobs_graph_validate_fails_if_indirect_cycle();
  test_jobs_graph_task_span_serial_chain();
  test_jobs_graph_task_span_parallel_chain();
  test_jobs_graph_task_span_complex_chain();
}
