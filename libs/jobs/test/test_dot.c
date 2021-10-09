#include "check_spec.h"
#include "core_alloc.h"
#include "jobs_dot.h"

spec(dot) {
  it("writes a Graph-Description-Language digraph based on a job-graph") {

    JobGraph* graph = jobs_graph_create(g_alloc_heap, string_lit("TestJob"), 2);

    const JobTaskId a = jobs_graph_add_task(graph, string_lit("A"), null, mem_empty);
    const JobTaskId b = jobs_graph_add_task(graph, string_lit("B"), null, mem_empty);
    const JobTaskId c = jobs_graph_add_task(graph, string_lit("C"), null, mem_empty);
    const JobTaskId d = jobs_graph_add_task(graph, string_lit("D"), null, mem_empty);
    const JobTaskId e = jobs_graph_add_task(graph, string_lit("E"), null, mem_empty);
    const JobTaskId f = jobs_graph_add_task(graph, string_lit("F"), null, mem_empty);
    const JobTaskId g = jobs_graph_add_task(graph, string_lit("G"), null, mem_empty);
    jobs_graph_add_task(graph, string_lit("H"), null, mem_empty);

    jobs_graph_task_depend(graph, a, b);
    jobs_graph_task_depend(graph, a, c);
    jobs_graph_task_depend(graph, b, d);
    jobs_graph_task_depend(graph, c, d);
    jobs_graph_task_depend(graph, d, e);
    jobs_graph_task_depend(graph, f, e);
    jobs_graph_task_depend(graph, g, d);

    check(jobs_graph_validate(graph));
    check(jobs_graph_task_span(graph) == 4);

    DynString buffer = dynstring_create(g_alloc_heap, 1024);
    jobs_dot_write_graph(&buffer, graph);

    check_eq_string(
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
                   "}\n"));

    dynstring_destroy(&buffer);
    jobs_graph_destroy(graph);
  }
}
