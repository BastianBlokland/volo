#include "core_alloc.h"
#include "core_file.h"
#include "core_format.h"
#include "jobs_dot.h"

#define dot_start_shape "octagon"
#define dot_end_shape "octagon"
#define dot_task_shape "box"

static void dot_write_task_node(DynString* str, JobGraph* graph, const JobTaskId taskId) {
  fmt_write(
      str,
      "  task_{} [label=\"{}\", shape=" dot_task_shape "];\n",
      fmt_int(taskId),
      fmt_text(jobs_graph_task_name(graph, taskId)));
}

static void dot_write_task_child_edges(DynString* str, JobGraph* graph, const JobTaskId taskId) {
  fmt_write(str, "  task_{} -> {", fmt_int(taskId));

  bool elemWritten = false;
  jobs_graph_for_task_child(graph, taskId, child, {
    fmt_write(str, "{}task_{}", elemWritten ? fmt_text_lit(", ") : fmt_nop(), fmt_int(child.task));
    elemWritten = true;
  });

  if (!elemWritten) {
    fmt_write(str, "end"); // If we have no child then the job's end depends on us.
  }
  fmt_write(str, "};\n", fmt_int(taskId));
}

static void dot_write_start_task_edges(DynString* str, JobGraph* graph) {
  fmt_write(str, "  start -> {");
  bool elemWritten = false;
  jobs_graph_for_task(graph, taskId, {
    if (jobs_graph_task_has_parent(graph, taskId)) {
      continue;
    }
    fmt_write(str, "{}task_{}", elemWritten ? fmt_text_lit(", ") : fmt_nop(), fmt_int(taskId));
    elemWritten = true;
  });
  fmt_write(str, "}\n");
}

void jobs_dot_write_graph(DynString* str, JobGraph* graph) {
  fmt_write(
      str,
      "digraph {} {\n"
      "  start [label=\"JobStart\", shape=" dot_start_shape "];\n"
      "  end [label=\"JobEnd\", shape=" dot_end_shape "];\n\n",
      fmt_text(jobs_graph_name(graph)));

  // Write task nodes.
  jobs_graph_for_task(graph, taskId, { dot_write_task_node(str, graph, taskId); });
  fmt_write(str, "\n");

  // Add edges from the start node to tasks without parents.
  dot_write_start_task_edges(str, graph);
  fmt_write(str, "\n");

  // Add edges from tasks to other task nodes (or the end node).
  jobs_graph_for_task(graph, taskId, { dot_write_task_child_edges(str, graph, taskId); });
  fmt_write(str, "}\n");
}

void jobs_dot_dump_graph(File* file, JobGraph* graph) {
  DynString buffer = dynstring_create(g_alloc_heap, usize_kibibyte);
  jobs_dot_write_graph(&buffer, graph);
  file_write_sync(file, dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}
