#include "check_spec.h"
#include "core_alloc.h"
#include "jobs_graph.h"

#define task_flags JobTaskFlags_None

spec(graph) {

  JobGraph* graph = null;

  setup() { graph = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 0); }

  it("stores a graph name") { check_eq_string(jobs_graph_name(graph), string_lit("TestJob")); }

  it("stores task names") {
    const JobTaskId taskA =
        jobs_graph_add_task(graph, string_lit("TestTaskA"), null, mem_empty, task_flags);
    const JobTaskId taskB =
        jobs_graph_add_task(graph, string_lit("TestTaskB"), null, mem_empty, task_flags);

    check_eq_int(jobs_graph_task_count(graph), 2);
    check_eq_string(jobs_graph_task_name(graph, taskA), string_lit("TestTaskA"));
    check_eq_string(jobs_graph_task_name(graph, taskB), string_lit("TestTaskB"));
  }

  it("can be copied") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);

    // Setup B to depend on A.
    jobs_graph_task_depend(graph, a, b);

    JobGraph* graphCopy = jobs_graph_create(g_alloc_heap, string_lit("TestJob2"), 0);
    jobs_graph_copy(graphCopy, graph);

    // Graphs should have identical task counts.
    check_eq_int(jobs_graph_task_count(graphCopy), jobs_graph_task_count(graph));
    check_eq_int(jobs_graph_task_root_count(graphCopy), jobs_graph_task_root_count(graph));
    check_eq_int(jobs_graph_task_leaf_count(graphCopy), jobs_graph_task_leaf_count(graph));

    // B should have a parent and A should not.
    check(jobs_graph_task_has_parent(graphCopy, b));
    check(!jobs_graph_task_has_parent(graphCopy, a));

    // A should have a child while B should not.
    check(jobs_graph_task_has_child(graphCopy, a));
    check(!jobs_graph_task_has_child(graphCopy, b));

    check_eq_int(jobs_graph_task_child_begin(graphCopy, a).task, b);
    check(sentinel_check(jobs_graph_task_child_begin(graphCopy, b).task));

    jobs_graph_destroy(graphCopy);
  }

  it("supports registering dependencies between tasks") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);

    // Setup B to depend on A.
    jobs_graph_task_depend(graph, a, b);

    // Meaning B has a parent and A does not.
    check(jobs_graph_task_has_parent(graph, b));
    check(!jobs_graph_task_has_parent(graph, a));

    // And A has a child while B does not.
    check(jobs_graph_task_has_child(graph, a));
    check(!jobs_graph_task_has_child(graph, b));

    check_eq_int(jobs_graph_task_child_begin(graph, a).task, b);
    check(sentinel_check(jobs_graph_task_child_begin(graph, b).task));
  }

  it("supports unregistering a dependency between tasks") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);

    // Setup B to depend on A.
    jobs_graph_task_depend(graph, a, b);

    // Remove the dependency from A to B.
    check(jobs_graph_task_undepend(graph, a, b));

    // So it cannot be removed again.
    check(!jobs_graph_task_undepend(graph, a, b));

    // Meaning neither have a parent.
    check(!jobs_graph_task_has_parent(graph, b));
    check(!jobs_graph_task_has_parent(graph, a));

    // And neither have a child.
    check(!jobs_graph_task_has_child(graph, a));
    check(!jobs_graph_task_has_child(graph, b));

    check(sentinel_check(jobs_graph_task_child_begin(graph, a).task));
    check(sentinel_check(jobs_graph_task_child_begin(graph, b).task));
  }

  it("supports unregistering multiple dependencies") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);

    // Setup B and C to depend on A.
    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, a, c);

    // Remove the dependencies.
    check(jobs_graph_task_undepend(graph, a, b));
    check(jobs_graph_task_undepend(graph, a, c));

    // Meaning neither have a parent.
    check(!jobs_graph_task_has_parent(graph, b));
    check(!jobs_graph_task_has_parent(graph, a));

    // And neither have a child.
    check(!jobs_graph_task_has_child(graph, a));
    check(!jobs_graph_task_has_child(graph, b));

    check(sentinel_check(jobs_graph_task_child_begin(graph, a).task));
    check(sentinel_check(jobs_graph_task_child_begin(graph, b).task));
  }

  it("cannot remove dependencies that do not exist") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);

    check(!jobs_graph_task_undepend(graph, a, b));
    check(!jobs_graph_task_undepend(graph, b, a));
  }

  it("supports graphs with many-to-one dependencies") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);

    check_eq_int(jobs_graph_task_count(graph), 4);

    // Setup D to depend on A, B and C.
    jobs_graph_task_depend(graph, a, d);
    jobs_graph_task_depend(graph, b, d);
    jobs_graph_task_depend(graph, c, d);

    check_eq_int(jobs_graph_task_span(graph), 2);
    check(jobs_graph_validate(graph));
    check_eq_int(jobs_graph_task_root_count(graph), 3);
    check_eq_int(jobs_graph_task_leaf_count(graph), 1);

    // Meaning only D has a parent.
    check(jobs_graph_task_has_parent(graph, d));
    check(!jobs_graph_task_has_parent(graph, a));
    check(!jobs_graph_task_has_parent(graph, b));
    check(!jobs_graph_task_has_parent(graph, c));

    // And A, B, C have a child.
    check(jobs_graph_task_has_child(graph, a));
    check(jobs_graph_task_has_child(graph, b));
    check(jobs_graph_task_has_child(graph, c));
    check(!jobs_graph_task_has_child(graph, d));

    check_eq_int(jobs_graph_task_child_begin(graph, a).task, d);
    check_eq_int(jobs_graph_task_child_begin(graph, b).task, d);
    check_eq_int(jobs_graph_task_child_begin(graph, c).task, d);
    check(sentinel_check(jobs_graph_task_child_begin(graph, d).task));
  }

  it("supports graphs with one-to-many dependencies") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);

    check_eq_int(jobs_graph_task_count(graph), 4);

    // Setup B, C, D to depend on A.
    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, a, c);
    jobs_graph_task_depend(graph, a, d);

    check(jobs_graph_validate(graph));
    check_eq_int(jobs_graph_task_span(graph), 2);
    check_eq_int(jobs_graph_task_root_count(graph), 1);
    check_eq_int(jobs_graph_task_leaf_count(graph), 3);

    // Meaning B, C, D have a parent.
    check(!jobs_graph_task_has_parent(graph, a));
    check(jobs_graph_task_has_parent(graph, b));
    check(jobs_graph_task_has_parent(graph, c));
    check(jobs_graph_task_has_parent(graph, d));

    // And only A has a child.
    check(jobs_graph_task_has_child(graph, a));
    check(!jobs_graph_task_has_child(graph, b));
    check(!jobs_graph_task_has_child(graph, c));
    check(!jobs_graph_task_has_child(graph, d));

    // Verify A has B, C, D as children.
    // TODO: Should we guarantee the order of dependencies like this? The current implementation
    // does keep the order but there is no real reason we need to.
    JobTaskChildItr itr = jobs_graph_task_child_begin(graph, a);
    check_eq_int(itr.task, b);
    itr = jobs_graph_task_child_next(graph, itr);
    check_eq_int(itr.task, c);
    itr = jobs_graph_task_child_next(graph, itr);
    check_eq_int(itr.task, d);
    itr = jobs_graph_task_child_next(graph, itr);
    check(sentinel_check(itr.task));
  }

  it("can reduce unnecessary dependencies in a linear graph") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);

    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, a, c);
    jobs_graph_task_depend(graph, a, d);

    jobs_graph_task_depend(graph, b, c);
    jobs_graph_task_depend(graph, b, d);

    jobs_graph_task_depend(graph, c, d);

    check_eq_int(jobs_graph_task_span(graph), 4); // Span of this graph is 4.

    // Three of these dependencies are unnecessary.
    check_eq_int(jobs_graph_reduce_dependencies(graph), 3);

    check_eq_int(jobs_graph_task_span(graph), 4); // Span is still 4.

    // A simple linear chain remains: A -> B -> C -> D.
    check_eq_int(jobs_graph_task_parent_count(graph, a), 0);
    check_eq_int(jobs_graph_task_parent_count(graph, b), 1);
    check_eq_int(jobs_graph_task_parent_count(graph, c), 1);
    check_eq_int(jobs_graph_task_parent_count(graph, d), 1);

    check_eq_int(jobs_graph_task_child_begin(graph, a).task, b);
    check_eq_int(jobs_graph_task_child_begin(graph, b).task, c);
    check_eq_int(jobs_graph_task_child_begin(graph, c).task, d);
    check(sentinel_check(jobs_graph_task_child_begin(graph, d).task));
  }

  it("can reduce unnecessary dependencies in a graph") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);
    const JobTaskId e = jobs_graph_add_task(graph, string_lit("E"), null, mem_empty, task_flags);

    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, a, c);
    jobs_graph_task_depend(graph, b, c);
    jobs_graph_task_depend(graph, d, b);
    jobs_graph_task_depend(graph, d, e);
    jobs_graph_task_depend(graph, d, c);
    jobs_graph_task_depend(graph, e, c);

    check_eq_int(jobs_graph_task_span(graph), 3); // Span of this graph is 3.

    // Two of these dependencies are unnecessary.
    check_eq_int(jobs_graph_reduce_dependencies(graph), 2);

    check_eq_int(jobs_graph_task_span(graph), 3); // Span of this graph is still 3.
  }

  it("cant reduce dependencies in a fully parallel graph") {
    jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("E"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("F"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("G"), null, mem_empty, task_flags);

    check_eq_int(jobs_graph_task_span(graph), 1); // Span of this graph is 1.

    // There are no dependencies to reduce.
    check_eq_int(jobs_graph_reduce_dependencies(graph), 0);

    check_eq_int(jobs_graph_task_span(graph), 1); // Span of this graph is still 1.
  }

  it("can detect cycles") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);

    // Setup cycle between A and B.
    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, b, a);

    check(!jobs_graph_validate(graph));
  }

  it("can detect indirect cycles") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);
    const JobTaskId e = jobs_graph_add_task(graph, string_lit("E"), null, mem_empty, task_flags);
    const JobTaskId f = jobs_graph_add_task(graph, string_lit("F"), null, mem_empty, task_flags);
    const JobTaskId g = jobs_graph_add_task(graph, string_lit("G"), null, mem_empty, task_flags);

    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, a, c);
    jobs_graph_task_depend(graph, b, d);
    jobs_graph_task_depend(graph, c, d);
    jobs_graph_task_depend(graph, d, e);
    jobs_graph_task_depend(graph, f, e);
    jobs_graph_task_depend(graph, g, d);
    jobs_graph_task_depend(graph, e, c);

    check(!jobs_graph_validate(graph));
  }

  it("can compute the span of a serial graph") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);
    const JobTaskId e = jobs_graph_add_task(graph, string_lit("E"), null, mem_empty, task_flags);
    const JobTaskId f = jobs_graph_add_task(graph, string_lit("F"), null, mem_empty, task_flags);
    const JobTaskId g = jobs_graph_add_task(graph, string_lit("G"), null, mem_empty, task_flags);

    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, b, c);
    jobs_graph_task_depend(graph, c, d);
    jobs_graph_task_depend(graph, d, e);
    jobs_graph_task_depend(graph, e, f);
    jobs_graph_task_depend(graph, f, g);

    check(jobs_graph_validate(graph));
    check_eq_int(jobs_graph_task_span(graph), 7);
    check_eq_int(jobs_graph_task_root_count(graph), 1);
    check_eq_int(jobs_graph_task_leaf_count(graph), 1);
  }

  it("can compute the span of a parallel graph") {
    jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("E"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("F"), null, mem_empty, task_flags);
    jobs_graph_add_task(graph, string_lit("G"), null, mem_empty, task_flags);

    check(jobs_graph_validate(graph));
    check_eq_int(jobs_graph_task_span(graph), 1);
    check_eq_int(jobs_graph_task_root_count(graph), 7);
    check_eq_int(jobs_graph_task_leaf_count(graph), 7);
  }

  it("can compute the span of a complex graph") {
    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty, task_flags);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty, task_flags);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty, task_flags);
    const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, mem_empty, task_flags);
    const JobTaskId e = jobs_graph_add_task(graph, string_lit("E"), null, mem_empty, task_flags);
    const JobTaskId f = jobs_graph_add_task(graph, string_lit("F"), null, mem_empty, task_flags);
    const JobTaskId g = jobs_graph_add_task(graph, string_lit("G"), null, mem_empty, task_flags);
    const JobTaskId h = jobs_graph_add_task(graph, string_lit("H"), null, mem_empty, task_flags);
    const JobTaskId i = jobs_graph_add_task(graph, string_lit("I"), null, mem_empty, task_flags);
    const JobTaskId j = jobs_graph_add_task(graph, string_lit("J"), null, mem_empty, task_flags);
    const JobTaskId k = jobs_graph_add_task(graph, string_lit("K"), null, mem_empty, task_flags);
    const JobTaskId l = jobs_graph_add_task(graph, string_lit("L"), null, mem_empty, task_flags);
    const JobTaskId m = jobs_graph_add_task(graph, string_lit("M"), null, mem_empty, task_flags);
    const JobTaskId n = jobs_graph_add_task(graph, string_lit("N"), null, mem_empty, task_flags);
    const JobTaskId o = jobs_graph_add_task(graph, string_lit("O"), null, mem_empty, task_flags);
    const JobTaskId p = jobs_graph_add_task(graph, string_lit("P"), null, mem_empty, task_flags);
    const JobTaskId q = jobs_graph_add_task(graph, string_lit("Q"), null, mem_empty, task_flags);
    const JobTaskId r = jobs_graph_add_task(graph, string_lit("R"), null, mem_empty, task_flags);

    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, b, c);
    jobs_graph_task_depend(graph, b, n);
    jobs_graph_task_depend(graph, c, d);
    jobs_graph_task_depend(graph, c, e);
    jobs_graph_task_depend(graph, e, f);
    jobs_graph_task_depend(graph, e, g);
    jobs_graph_task_depend(graph, f, h);
    jobs_graph_task_depend(graph, g, i);
    jobs_graph_task_depend(graph, d, j);
    jobs_graph_task_depend(graph, j, k);
    jobs_graph_task_depend(graph, h, k);
    jobs_graph_task_depend(graph, i, k);
    jobs_graph_task_depend(graph, k, l);
    jobs_graph_task_depend(graph, l, m);
    jobs_graph_task_depend(graph, n, q);
    jobs_graph_task_depend(graph, n, r);
    jobs_graph_task_depend(graph, q, o);
    jobs_graph_task_depend(graph, r, p);
    jobs_graph_task_depend(graph, o, m);
    jobs_graph_task_depend(graph, p, m);

    // Verify that there are no redundant dependencies.
    check_eq_int(jobs_graph_reduce_dependencies(graph), 0);

    check(jobs_graph_validate(graph));
    check_eq_int(jobs_graph_task_span(graph), 9);
    check_eq_float(jobs_graph_task_parallelism(graph), 2.0f, 1e-6f);
    check_eq_int(jobs_graph_task_root_count(graph), 1);
    check_eq_int(jobs_graph_task_leaf_count(graph), 1);
  }

  teardown() { jobs_graph_destroy(graph); }
}
