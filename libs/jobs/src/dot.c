#include "core_alloc.h"
#include "core_file.h"
#include "core_format.h"
#include "jobs_dot.h"

#define dot_start_shape "octagon"
#define dot_end_shape "octagon"
#define dot_task_shape "box"

static void dot_write_task_node(DynString* str, JobDef* job, const JobTaskId taskId) {
  fmt_write(
      str,
      "  task_{} [label=\"{}\", shape=" dot_task_shape "];\n",
      fmt_int(taskId),
      fmt_text(jobdef_task_name(job, taskId)));
}

static void dot_write_task_child_edges(DynString* str, JobDef* job, const JobTaskId taskId) {
  fmt_write(str, "  task_{} -> {", fmt_int(taskId));

  bool elemWritten = false;
  jobdef_for_task_child(job, taskId, child, {
    fmt_write(str, "{}task_{}", elemWritten ? fmt_text_lit(", ") : fmt_nop(), fmt_int(child.task));
    elemWritten = true;
  });

  if (!elemWritten) {
    fmt_write(str, "end"); // If we have no child then the job's end depends on us.
  }
  fmt_write(str, "};\n", fmt_int(taskId));
}

static void dot_write_start_task_edges(DynString* str, JobDef* job) {
  fmt_write(str, "  start -> {");
  bool elemWritten = false;
  jobdef_for_task(job, taskId, {
    if (jobdef_task_has_parent(job, taskId)) {
      continue;
    }
    fmt_write(str, "{}task_{}", elemWritten ? fmt_text_lit(", ") : fmt_nop(), fmt_int(taskId));
    elemWritten = true;
  });
  fmt_write(str, "}\n");
}

void jobs_dot_write_jobdef(DynString* str, JobDef* job) {
  fmt_write(
      str,
      "digraph {} {\n"
      "  start [label=\"JobStart\", shape=" dot_start_shape "];\n"
      "  end [label=\"JobEnd\", shape=" dot_end_shape "];\n\n",
      fmt_text(jobdef_job_name(job)));

  // Write task nodes.
  jobdef_for_task(job, taskId, { dot_write_task_node(str, job, taskId); });
  fmt_write(str, "\n");

  // Add edges from the start node to tasks without parents.
  dot_write_start_task_edges(str, job);
  fmt_write(str, "\n");

  // Add edges from tasks to other task nodes (or the end node).
  jobdef_for_task(job, taskId, { dot_write_task_child_edges(str, job, taskId); });
  fmt_write(str, "}\n");
}

void jobs_dot_dump_jobdef(File* file, JobDef* job) {
  DynString buffer = dynstring_create(g_alloc_heap, usize_kibibyte);
  jobs_dot_write_jobdef(&buffer, job);
  file_write_sync(file, dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}
