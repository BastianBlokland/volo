#include "core_diag.h"
#include "jobs_dot.h"

static void test_dot_graph() {
  Allocator* alloc = alloc_bump_create_stack(2048);
  JobGraph*  graph = jobs_graph_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, null);
  const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, null);
  const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, null);
  const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, null);
  const JobTaskId e = jobs_graph_add_task(graph, string_lit("E"), null, null);
  const JobTaskId f = jobs_graph_add_task(graph, string_lit("F"), null, null);
  const JobTaskId g = jobs_graph_add_task(graph, string_lit("G"), null, null);
  jobs_graph_add_task(graph, string_lit("H"), null, null);

  jobs_graph_task_depend(graph, a, b);
  jobs_graph_task_depend(graph, a, c);
  jobs_graph_task_depend(graph, b, d);
  jobs_graph_task_depend(graph, c, d);
  jobs_graph_task_depend(graph, d, e);
  jobs_graph_task_depend(graph, f, e);
  jobs_graph_task_depend(graph, g, d);

  diag_assert(jobs_graph_validate(graph));
  diag_assert(jobs_graph_task_span(graph) == 4);

  DynString buffer = dynstring_create(alloc, 1024);
  jobs_dot_write_graph(&buffer, graph);
  diag_assert(string_eq(
      dynstring_view(&buffer),
      string_lit("digraph TestJob {\n"
                 "  start [label=\"JobStart\", shape=octagon];\n"
                 "  end [label=\"JobEnd\", shape=octagon];\n"
                 "\n"
                 "  task_0 [label=\"A\", shape=box];\n"
                 "  task_1 [label=\"B\", shape=box];\n"
                 "  task_2 [label=\"C\", shape=box];\n"
                 "  task_3 [label=\"D\", shape=box];\n"
                 "  task_4 [label=\"E\", shape=box];\n"
                 "  task_5 [label=\"F\", shape=box];\n"
                 "  task_6 [label=\"G\", shape=box];\n"
                 "  task_7 [label=\"H\", shape=box];\n"
                 "\n"
                 "  start -> {task_0, task_5, task_6, task_7}\n"
                 "\n"
                 "  task_0 -> {task_1, task_2};\n"
                 "  task_1 -> {task_3};\n"
                 "  task_2 -> {task_3};\n"
                 "  task_3 -> {task_4};\n"
                 "  task_4 -> {end};\n"
                 "  task_5 -> {task_4};\n"
                 "  task_6 -> {task_3};\n"
                 "  task_7 -> {end};\n"
                 "}\n")));
  dynstring_destroy(&buffer);

  jobs_graph_destroy(graph);
}

void test_dot() { test_dot_graph(); }
