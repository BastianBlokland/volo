#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_runner.h"
#include "ecs_world.h"
#include "jobs_graph.h"

ecs_comp_define(GraphCompA) { u32 f1; };
ecs_comp_define(GraphCompB) { u32 f1; };
ecs_comp_define(GraphCompC) { u32 f1; };

ecs_view_define(WriteA) { ecs_access_write(GraphCompA); }
ecs_view_define(WriteC) { ecs_access_write(GraphCompC); }

ecs_view_define(ReadABWithoutC) {
  ecs_access_without(GraphCompC);
  ecs_access_read(GraphCompA);
  ecs_access_read(GraphCompB);
}

ecs_view_define(ReadAWriteBC) {
  ecs_access_read(GraphCompA);
  ecs_access_write(GraphCompB);
  ecs_access_write(GraphCompC);
}

ecs_view_define(WriteCWithoutA) {
  ecs_access_without(GraphCompA);
  ecs_access_write(GraphCompC);
}

ecs_view_define(ReadABC) {
  ecs_access_read(GraphCompA);
  ecs_access_read(GraphCompB);
  ecs_access_read(GraphCompC);
}

ecs_system_define(GraphSys1) {}
ecs_system_define(GraphSys2) {}
ecs_system_define(GraphSys3) {}
ecs_system_define(GraphSys4) {}
ecs_system_define(GraphSys5) {}

ecs_module_init(graph_test_module) {
  ecs_register_comp(GraphCompA);
  ecs_register_comp(GraphCompB);
  ecs_register_comp(GraphCompC);

  ecs_register_view(ReadABWithoutC);
  ecs_register_view(WriteA);
  ecs_register_view(WriteC);
  ecs_register_view(ReadAWriteBC);
  ecs_register_view(WriteCWithoutA);
  ecs_register_view(ReadABC);

  ecs_register_system(GraphSys1, ecs_view_id(WriteA), ecs_view_id(WriteC));
  ecs_order(GraphSys1, 1);

  ecs_register_system(GraphSys5, ecs_view_id(ReadABC));
  ecs_order(GraphSys5, 5);

  ecs_register_system(GraphSys2, ecs_view_id(ReadAWriteBC));
  ecs_order(GraphSys2, 2);

  ecs_register_system(GraphSys4, ecs_view_id(WriteCWithoutA), ecs_view_id(ReadABWithoutC));
  ecs_order(GraphSys4, 4);

  ecs_register_system(GraphSys3, ecs_view_id(ReadABWithoutC));
  ecs_order(GraphSys3, 3);
}

spec(graph) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, graph_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("inserts job-graph tasks for all systems") {
    const JobGraph* graph = ecs_runner_graph(runner);

    const JobTaskId sys1Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys1)).begin;
    check_eq_string(
        jobs_graph_task_name(graph, sys1Task), ecs_def_system_name(def, ecs_system_id(GraphSys1)));

    const JobTaskId sys2Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys2)).begin;
    check_eq_string(
        jobs_graph_task_name(graph, sys2Task), ecs_def_system_name(def, ecs_system_id(GraphSys2)));

    const JobTaskId sys3Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys3)).begin;
    check_eq_string(
        jobs_graph_task_name(graph, sys3Task), ecs_def_system_name(def, ecs_system_id(GraphSys3)));

    const JobTaskId sys4Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys4)).begin;
    check_eq_string(
        jobs_graph_task_name(graph, sys4Task), ecs_def_system_name(def, ecs_system_id(GraphSys4)));

    const JobTaskId sys5Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys5)).begin;
    check_eq_string(
        jobs_graph_task_name(graph, sys5Task), ecs_def_system_name(def, ecs_system_id(GraphSys5)));
  }

  it("creates task dependencies based on the system views") {
    const JobGraph* graph = ecs_runner_graph(runner);

    const JobTaskId sys1Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys1)).begin;
    const JobTaskId sys2Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys2)).begin;
    const JobTaskId sys3Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys3)).begin;
    const JobTaskId sys4Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys4)).begin;
    const JobTaskId sys5Task = ecs_runner_task_set(runner, ecs_system_id(GraphSys5)).begin;

    check(!jobs_graph_task_has_parent(graph, sys1Task));

    // System 2, 3 and 4 all depend on system 1.
    JobTaskChildItr sys1ChildItr = jobs_graph_task_child_begin(graph, sys1Task);
    check_eq_int(sys1ChildItr.task, sys2Task);
    sys1ChildItr = jobs_graph_task_child_next(graph, sys1ChildItr);
    check_eq_int(sys1ChildItr.task, sys3Task);
    sys1ChildItr = jobs_graph_task_child_next(graph, sys1ChildItr);
    check_eq_int(sys1ChildItr.task, sys4Task);

    // System 5 depends on system 2.
    check_eq_int(jobs_graph_task_child_begin(graph, sys2Task).task, sys5Task);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
