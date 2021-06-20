#include "core_diag.h"
#include "jobs_jobdef.h"

static void test_jobdef_job_name_can_be_retrieved() {
  Allocator* alloc = alloc_bump_create_stack(256);
  JobDef*    job   = jobdef_create(alloc, string_lit("TestJob"), 0);

  diag_assert(string_eq(jobdef_job_name(job), string_lit("TestJob")));

  jobdef_destroy(job);
}

static void test_jobdef_task_name_can_be_retrieved() {
  Allocator* alloc = alloc_bump_create_stack(512);
  JobDef*    job   = jobdef_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId taskA = jobdef_add_task(job, string_lit("TestTaskA"), null, null);
  const JobTaskId taskB = jobdef_add_task(job, string_lit("TestTaskB"), null, null);

  diag_assert(jobdef_task_count(job) == 2);
  diag_assert(string_eq(jobdef_task_name(job, taskA), string_lit("TestTaskA")));
  diag_assert(string_eq(jobdef_task_name(job, taskB), string_lit("TestTaskB")));

  jobdef_destroy(job);
}

static void test_jobdef_many_to_one_dependency() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobDef*    job   = jobdef_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobdef_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobdef_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobdef_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobdef_add_task(job, string_lit("D"), null, null);

  diag_assert(jobdef_task_count(job) == 4);

  // Setup D to depend on A, B and C.
  jobdef_task_depend(job, a, d);
  jobdef_task_depend(job, b, d);
  jobdef_task_depend(job, c, d);

  diag_assert(jobdef_validate(job));

  // Meaning only D has a parent.
  diag_assert(jobdef_task_has_parent(job, d));
  diag_assert(!jobdef_task_has_parent(job, a));
  diag_assert(!jobdef_task_has_parent(job, b));
  diag_assert(!jobdef_task_has_parent(job, c));

  // And A, B, C have a child.
  diag_assert(jobdef_task_has_child(job, a));
  diag_assert(jobdef_task_has_child(job, b));
  diag_assert(jobdef_task_has_child(job, c));
  diag_assert(!jobdef_task_has_child(job, d));

  diag_assert(jobdef_task_child_begin(job, a).task == d);
  diag_assert(jobdef_task_child_begin(job, b).task == d);
  diag_assert(jobdef_task_child_begin(job, c).task == d);
  diag_assert(sentinel_check(jobdef_task_child_begin(job, d).task));

  jobdef_destroy(job);
}

static void test_jobdef_one_to_many_dependency() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobDef*    job   = jobdef_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobdef_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobdef_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobdef_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobdef_add_task(job, string_lit("D"), null, null);

  diag_assert(jobdef_task_count(job) == 4);

  // Setup B, C, D to depend on A.
  jobdef_task_depend(job, a, b);
  jobdef_task_depend(job, a, c);
  jobdef_task_depend(job, a, d);

  diag_assert(jobdef_validate(job));

  // Meaning B, C, D have a parent.
  diag_assert(!jobdef_task_has_parent(job, a));
  diag_assert(jobdef_task_has_parent(job, b));
  diag_assert(jobdef_task_has_parent(job, c));
  diag_assert(jobdef_task_has_parent(job, d));

  // And only A has a child.
  diag_assert(jobdef_task_has_child(job, a));
  diag_assert(!jobdef_task_has_child(job, b));
  diag_assert(!jobdef_task_has_child(job, c));
  diag_assert(!jobdef_task_has_child(job, d));

  // Verify A has B, C, D as children.
  // TODO: Should we guarantee the order of dependencies like this? The current implementation does
  // keep the order but there is no real reason we need to.
  JobTaskChildItr itr = jobdef_task_child_begin(job, a);
  diag_assert(itr.task == b);
  itr = jobdef_task_child_next(job, itr);
  diag_assert(itr.task == c);
  itr = jobdef_task_child_next(job, itr);
  diag_assert(itr.task == d);
  itr = jobdef_task_child_next(job, itr);
  diag_assert(sentinel_check(itr.task));

  jobdef_destroy(job);
}

static void test_jobdef_validate_fails_if_cycle() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobDef*    job   = jobdef_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobdef_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobdef_add_task(job, string_lit("B"), null, null);

  // Setup cycle between A and B.
  jobdef_task_depend(job, a, b);
  jobdef_task_depend(job, b, a);

  diag_assert(!jobdef_validate(job));

  jobdef_destroy(job);
}

static void test_jobdef_validate_fails_if_indirect_cycle() {
  Allocator* alloc = alloc_bump_create_stack(1024);
  JobDef*    job   = jobdef_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobdef_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobdef_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobdef_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobdef_add_task(job, string_lit("D"), null, null);
  const JobTaskId e = jobdef_add_task(job, string_lit("E"), null, null);
  const JobTaskId f = jobdef_add_task(job, string_lit("F"), null, null);
  const JobTaskId g = jobdef_add_task(job, string_lit("G"), null, null);

  jobdef_task_depend(job, a, b);
  jobdef_task_depend(job, a, c);
  jobdef_task_depend(job, b, d);
  jobdef_task_depend(job, c, d);
  jobdef_task_depend(job, d, e);
  jobdef_task_depend(job, f, e);
  jobdef_task_depend(job, g, d);
  jobdef_task_depend(job, e, c);

  diag_assert(!jobdef_validate(job));

  jobdef_destroy(job);
}

void test_jobdef() {
  test_jobdef_job_name_can_be_retrieved();
  test_jobdef_task_name_can_be_retrieved();
  test_jobdef_many_to_one_dependency();
  test_jobdef_one_to_many_dependency();
  test_jobdef_validate_fails_if_cycle();
  test_jobdef_validate_fails_if_indirect_cycle();
}
