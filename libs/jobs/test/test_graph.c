#include "check_spec.h"
#include "core_alloc.h"
#include "jobs_graph.h"

#define task_flags JobTaskFlags_None

spec(graph) {

  JobGraph* job = null;

  setup() { job = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 0); }

  it("stores a graph name") { check_eq_string(jobs_graph_name(job), string_lit("TestJob")); }

  it("stores task names") {
    const JobTaskId taskA =
        jobs_graph_add_task(job, string_lit("TestTaskA"), null, mem_empty, task_flags);
    const JobTaskId taskB =
        jobs_graph_add_task(job, string_lit("TestTaskB"), null, mem_empty, task_flags);

    check_eq_int(jobs_graph_task_count(job), 2);
    check_eq_string(jobs_graph_task_name(job, taskA), string_lit("TestTaskA"));
    check_eq_string(jobs_graph_task_name(job, taskB), string_lit("TestTaskB"));
  }

  it("supports registering dependencies between tasks") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);

    // Setup B to depend on A.
    jobs_graph_task_depend(job, a, b);

    // Meaning B has a parent and A does not.
    check(jobs_graph_task_has_parent(job, b));
    check(!jobs_graph_task_has_parent(job, a));

    // And A has a child while B does not.
    check(jobs_graph_task_has_child(job, a));
    check(!jobs_graph_task_has_child(job, b));

    check_eq_int(jobs_graph_task_child_begin(job, a).task, b);
    check(sentinel_check(jobs_graph_task_child_begin(job, b).task));
  }

  it("supports unregistering a dependency between tasks") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);

    // Setup B to depend on A.
    jobs_graph_task_depend(job, a, b);

    // Remove the dependency from A to B.
    check(jobs_graph_task_undepend(job, a, b));

    // So it cannot be removed again.
    check(!jobs_graph_task_undepend(job, a, b));

    // Meaning neither have a parent.
    check(!jobs_graph_task_has_parent(job, b));
    check(!jobs_graph_task_has_parent(job, a));

    // And neither have a child.
    check(!jobs_graph_task_has_child(job, a));
    check(!jobs_graph_task_has_child(job, b));

    check(sentinel_check(jobs_graph_task_child_begin(job, a).task));
    check(sentinel_check(jobs_graph_task_child_begin(job, b).task));
  }

  it("supports unregistering multiple dependencies") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);

    // Setup B and C to depend on A.
    jobs_graph_task_depend(job, a, b);
    jobs_graph_task_depend(job, a, c);

    // Remove the dependencies.
    check(jobs_graph_task_undepend(job, a, b));
    check(jobs_graph_task_undepend(job, a, c));

    // Meaning neither have a parent.
    check(!jobs_graph_task_has_parent(job, b));
    check(!jobs_graph_task_has_parent(job, a));

    // And neither have a child.
    check(!jobs_graph_task_has_child(job, a));
    check(!jobs_graph_task_has_child(job, b));

    check(sentinel_check(jobs_graph_task_child_begin(job, a).task));
    check(sentinel_check(jobs_graph_task_child_begin(job, b).task));
  }

  it("cannot remove dependencies that do not exist") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);

    check(!jobs_graph_task_undepend(job, a, b));
    check(!jobs_graph_task_undepend(job, b, a));
  }

  it("supports graphs with many-to-one dependencies") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);

    check_eq_int(jobs_graph_task_count(job), 4);

    // Setup D to depend on A, B and C.
    jobs_graph_task_depend(job, a, d);
    jobs_graph_task_depend(job, b, d);
    jobs_graph_task_depend(job, c, d);

    check_eq_int(jobs_graph_task_span(job), 2);
    check(jobs_graph_validate(job));
    check_eq_int(jobs_graph_task_root_count(job), 3);
    check_eq_int(jobs_graph_task_leaf_count(job), 1);

    // Meaning only D has a parent.
    check(jobs_graph_task_has_parent(job, d));
    check(!jobs_graph_task_has_parent(job, a));
    check(!jobs_graph_task_has_parent(job, b));
    check(!jobs_graph_task_has_parent(job, c));

    // And A, B, C have a child.
    check(jobs_graph_task_has_child(job, a));
    check(jobs_graph_task_has_child(job, b));
    check(jobs_graph_task_has_child(job, c));
    check(!jobs_graph_task_has_child(job, d));

    check_eq_int(jobs_graph_task_child_begin(job, a).task, d);
    check_eq_int(jobs_graph_task_child_begin(job, b).task, d);
    check_eq_int(jobs_graph_task_child_begin(job, c).task, d);
    check(sentinel_check(jobs_graph_task_child_begin(job, d).task));
  }

  it("supports graphs with one-to-many dependencies") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);

    check_eq_int(jobs_graph_task_count(job), 4);

    // Setup B, C, D to depend on A.
    jobs_graph_task_depend(job, a, b);
    jobs_graph_task_depend(job, a, c);
    jobs_graph_task_depend(job, a, d);

    check(jobs_graph_validate(job));
    check_eq_int(jobs_graph_task_span(job), 2);
    check_eq_int(jobs_graph_task_root_count(job), 1);
    check_eq_int(jobs_graph_task_leaf_count(job), 3);

    // Meaning B, C, D have a parent.
    check(!jobs_graph_task_has_parent(job, a));
    check(jobs_graph_task_has_parent(job, b));
    check(jobs_graph_task_has_parent(job, c));
    check(jobs_graph_task_has_parent(job, d));

    // And only A has a child.
    check(jobs_graph_task_has_child(job, a));
    check(!jobs_graph_task_has_child(job, b));
    check(!jobs_graph_task_has_child(job, c));
    check(!jobs_graph_task_has_child(job, d));

    // Verify A has B, C, D as children.
    // TODO: Should we guarantee the order of dependencies like this? The current implementation
    // does keep the order but there is no real reason we need to.
    JobTaskChildItr itr = jobs_graph_task_child_begin(job, a);
    check_eq_int(itr.task, b);
    itr = jobs_graph_task_child_next(job, itr);
    check_eq_int(itr.task, c);
    itr = jobs_graph_task_child_next(job, itr);
    check_eq_int(itr.task, d);
    itr = jobs_graph_task_child_next(job, itr);
    check(sentinel_check(itr.task));
  }

  it("can reduce unnecessary dependencies in a linear graph") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);

    jobs_graph_task_depend(job, a, b);
    jobs_graph_task_depend(job, a, c);
    jobs_graph_task_depend(job, a, d);

    jobs_graph_task_depend(job, b, c);
    jobs_graph_task_depend(job, b, d);

    jobs_graph_task_depend(job, c, d);

    check_eq_int(jobs_graph_task_span(job), 4); // Span of this graph is 4.

    // Three of these dependencies are unnecessary.
    check_eq_int(jobs_graph_reduce_dependencies(job), 3);

    check_eq_int(jobs_graph_task_span(job), 4); // Span is still 4.

    // A simple linear chain remains: A -> B -> C -> D.
    check_eq_int(jobs_graph_task_parent_count(job, a), 0);
    check_eq_int(jobs_graph_task_parent_count(job, b), 1);
    check_eq_int(jobs_graph_task_parent_count(job, c), 1);
    check_eq_int(jobs_graph_task_parent_count(job, d), 1);

    check_eq_int(jobs_graph_task_child_begin(job, a).task, b);
    check_eq_int(jobs_graph_task_child_begin(job, b).task, c);
    check_eq_int(jobs_graph_task_child_begin(job, c).task, d);
    check(sentinel_check(jobs_graph_task_child_begin(job, d).task));
  }

  it("can reduce unnecessary dependencies in a graph") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);
    const JobTaskId e = jobs_graph_add_task(job, string_lit("E"), null, mem_empty, task_flags);

    jobs_graph_task_depend(job, a, b);
    jobs_graph_task_depend(job, a, c);
    jobs_graph_task_depend(job, b, c);
    jobs_graph_task_depend(job, d, b);
    jobs_graph_task_depend(job, d, e);
    jobs_graph_task_depend(job, d, c);
    jobs_graph_task_depend(job, e, c);

    check_eq_int(jobs_graph_task_span(job), 3); // Span of this graph is 3.

    // Two of these dependencies are unnecessary.
    check_eq_int(jobs_graph_reduce_dependencies(job), 2);

    check_eq_int(jobs_graph_task_span(job), 3); // Span of this graph is still 3.
  }

  it("cant reduce dependencies in a fully parallel graph") {
    jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("E"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("F"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("G"), null, mem_empty, task_flags);

    check_eq_int(jobs_graph_task_span(job), 1); // Span of this graph is 1.

    // There are no dependencies to reduce.
    check_eq_int(jobs_graph_reduce_dependencies(job), 0);

    check_eq_int(jobs_graph_task_span(job), 1); // Span of this graph is still 1.
  }

  it("can detect cycles") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);

    // Setup cycle between A and B.
    jobs_graph_task_depend(job, a, b);
    jobs_graph_task_depend(job, b, a);

    check(!jobs_graph_validate(job));
  }

  it("can detect indirect cycles") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);
    const JobTaskId e = jobs_graph_add_task(job, string_lit("E"), null, mem_empty, task_flags);
    const JobTaskId f = jobs_graph_add_task(job, string_lit("F"), null, mem_empty, task_flags);
    const JobTaskId g = jobs_graph_add_task(job, string_lit("G"), null, mem_empty, task_flags);

    jobs_graph_task_depend(job, a, b);
    jobs_graph_task_depend(job, a, c);
    jobs_graph_task_depend(job, b, d);
    jobs_graph_task_depend(job, c, d);
    jobs_graph_task_depend(job, d, e);
    jobs_graph_task_depend(job, f, e);
    jobs_graph_task_depend(job, g, d);
    jobs_graph_task_depend(job, e, c);

    check(!jobs_graph_validate(job));
  }

  it("can compute the span of a serial graph") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);
    const JobTaskId e = jobs_graph_add_task(job, string_lit("E"), null, mem_empty, task_flags);
    const JobTaskId f = jobs_graph_add_task(job, string_lit("F"), null, mem_empty, task_flags);
    const JobTaskId g = jobs_graph_add_task(job, string_lit("G"), null, mem_empty, task_flags);

    jobs_graph_task_depend(job, a, b);
    jobs_graph_task_depend(job, b, c);
    jobs_graph_task_depend(job, c, d);
    jobs_graph_task_depend(job, d, e);
    jobs_graph_task_depend(job, e, f);
    jobs_graph_task_depend(job, f, g);

    check(jobs_graph_validate(job));
    check_eq_int(jobs_graph_task_span(job), 7);
    check_eq_int(jobs_graph_task_root_count(job), 1);
    check_eq_int(jobs_graph_task_leaf_count(job), 1);
  }

  it("can compute the span of a parallel graph") {
    jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("E"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("F"), null, mem_empty, task_flags);
    jobs_graph_add_task(job, string_lit("G"), null, mem_empty, task_flags);

    check(jobs_graph_validate(job));
    check_eq_int(jobs_graph_task_span(job), 1);
    check_eq_int(jobs_graph_task_root_count(job), 7);
    check_eq_int(jobs_graph_task_leaf_count(job), 7);
  }

  it("can compute the span of a complex graph") {
    const JobTaskId a = jobs_graph_add_task(job, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(job, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(job, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(job, string_lit("D"), null, mem_empty, task_flags);
    const JobTaskId e = jobs_graph_add_task(job, string_lit("E"), null, mem_empty, task_flags);
    const JobTaskId f = jobs_graph_add_task(job, string_lit("F"), null, mem_empty, task_flags);
    const JobTaskId g = jobs_graph_add_task(job, string_lit("G"), null, mem_empty, task_flags);
    const JobTaskId h = jobs_graph_add_task(job, string_lit("H"), null, mem_empty, task_flags);
    const JobTaskId i = jobs_graph_add_task(job, string_lit("I"), null, mem_empty, task_flags);
    const JobTaskId j = jobs_graph_add_task(job, string_lit("J"), null, mem_empty, task_flags);
    const JobTaskId k = jobs_graph_add_task(job, string_lit("K"), null, mem_empty, task_flags);
    const JobTaskId l = jobs_graph_add_task(job, string_lit("L"), null, mem_empty, task_flags);
    const JobTaskId m = jobs_graph_add_task(job, string_lit("M"), null, mem_empty, task_flags);
    const JobTaskId n = jobs_graph_add_task(job, string_lit("N"), null, mem_empty, task_flags);
    const JobTaskId o = jobs_graph_add_task(job, string_lit("O"), null, mem_empty, task_flags);
    const JobTaskId p = jobs_graph_add_task(job, string_lit("P"), null, mem_empty, task_flags);
    const JobTaskId q = jobs_graph_add_task(job, string_lit("Q"), null, mem_empty, task_flags);
    const JobTaskId r = jobs_graph_add_task(job, string_lit("R"), null, mem_empty, task_flags);

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

    // Verify that there are no redundant dependencies.
    check_eq_int(jobs_graph_reduce_dependencies(job), 0);

    check(jobs_graph_validate(job));
    check_eq_int(jobs_graph_task_span(job), 9);
    check_eq_float(jobs_graph_task_parallelism(job), 2.0f, 1e-6f);
    check_eq_int(jobs_graph_task_root_count(job), 1);
    check_eq_int(jobs_graph_task_leaf_count(job), 1);
  }

  teardown() { jobs_graph_destroy(job); }
}
