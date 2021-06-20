#include "core_diag.h"
#include "jobs_dot.h"

static void test_dot_jobdef_graph() {
  Allocator* alloc = alloc_bump_create_stack(2048);
  JobDef*    job   = jobdef_create(alloc, string_lit("TestJob"), 2);

  const JobTaskId a = jobdef_add_task(job, string_lit("A"), null, null);
  const JobTaskId b = jobdef_add_task(job, string_lit("B"), null, null);
  const JobTaskId c = jobdef_add_task(job, string_lit("C"), null, null);
  const JobTaskId d = jobdef_add_task(job, string_lit("D"), null, null);
  const JobTaskId e = jobdef_add_task(job, string_lit("E"), null, null);
  const JobTaskId f = jobdef_add_task(job, string_lit("F"), null, null);
  const JobTaskId g = jobdef_add_task(job, string_lit("G"), null, null);
  jobdef_add_task(job, string_lit("H"), null, null);

  jobdef_task_depend(job, a, b);
  jobdef_task_depend(job, a, c);
  jobdef_task_depend(job, b, d);
  jobdef_task_depend(job, c, d);
  jobdef_task_depend(job, d, e);
  jobdef_task_depend(job, f, e);
  jobdef_task_depend(job, g, d);

  diag_assert(jobdef_validate(job));

  DynString buffer = dynstring_create(alloc, 1024);
  jobs_dot_write_jobdef(&buffer, job);
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

  jobdef_destroy(job);
}

void test_dot() { test_dot_jobdef_graph(); }
