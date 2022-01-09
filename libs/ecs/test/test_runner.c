#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_def.h"
#include "ecs_runner.h"
#include "ecs_world.h"

ecs_comp_define(RunnerCompA) { u32 f1; };
ecs_comp_define(RunnerCompB) { u32 f1; };
ecs_comp_define(RunnerCompC) { u32 f1; };

ecs_view_define(ReadA) { ecs_access_read(RunnerCompA); }

ecs_view_define(ReadAWriteBC) {
  ecs_access_read(RunnerCompA);
  ecs_access_write(RunnerCompB);
  ecs_access_write(RunnerCompC);
}

ecs_view_define(ReadBWriteA) {
  ecs_access_read(RunnerCompB);
  ecs_access_write(RunnerCompA);
}

ecs_view_define(ReadCWriteA) {
  ecs_access_read(RunnerCompC);
  ecs_access_write(RunnerCompA);
}

ecs_system_define(RunnerSys1) {
  diag_assert(g_ecsRunningSystem);
  diag_assert(g_ecsRunningSystemId == ecs_system_id(RunnerSys1));
  diag_assert(ecs_world_busy(world));

  EcsView* view = ecs_world_view_t(world, ReadAWriteBC);
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    const RunnerCompA* compA = ecs_view_read_t(itr, RunnerCompA);
    RunnerCompB*       compB = ecs_view_write_t(itr, RunnerCompB);
    RunnerCompC*       compC = ecs_view_write_t(itr, RunnerCompC);

    compB->f1 = compA->f1 * 2;
    compC->f1 = compB->f1 / 4;
  }
}

ecs_system_define(RunnerSys2) {
  diag_assert(g_ecsRunningSystem);
  diag_assert(g_ecsRunningSystemId == ecs_system_id(RunnerSys2));
  diag_assert(ecs_world_busy(world));

  EcsView* view = ecs_world_view_t(world, ReadBWriteA);
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    const RunnerCompB* compB = ecs_view_read_t(itr, RunnerCompB);
    RunnerCompA*       compA = ecs_view_write_t(itr, RunnerCompA);

    compA->f1 += compB->f1 * 4;
  }
}

ecs_system_define(RunnerSys3) {
  diag_assert(g_ecsRunningSystem);
  diag_assert(g_ecsRunningSystemId == ecs_system_id(RunnerSys3));
  diag_assert(ecs_world_busy(world));

  EcsView* view = ecs_world_view_t(world, ReadCWriteA);
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    const RunnerCompC* compC = ecs_view_read_t(itr, RunnerCompC);
    RunnerCompA*       compA = ecs_view_write_t(itr, RunnerCompA);

    compA->f1 += compC->f1 * compC->f1;
  }
}

ecs_module_init(runner_test_module) {

  ecs_register_comp(RunnerCompA);
  ecs_register_comp(RunnerCompB);
  ecs_register_comp(RunnerCompC);

  ecs_register_view(ReadA);
  ecs_register_view(ReadAWriteBC);
  ecs_register_view(ReadBWriteA);
  ecs_register_view(ReadCWriteA);

  ecs_register_system(RunnerSys3, ecs_view_id(ReadCWriteA));
  ecs_order(RunnerSys3, 3);

  ecs_register_system(RunnerSys1, ecs_view_id(ReadAWriteBC));
  ecs_order(RunnerSys1, 1);

  ecs_register_system(RunnerSys2, ecs_view_id(ReadBWriteA));
  ecs_order(RunnerSys2, 2);
}

spec(runner) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, runner_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("executes every system once in specified order") {
    check(!g_ecsRunningSystem);
    diag_assert(g_ecsRunningSystemId == sentinel_u16);
    check(!ecs_world_busy(world));

    const EcsEntityId entity = ecs_world_entity_create(world);
    ecs_world_add_t(world, entity, RunnerCompA, .f1 = 42);
    ecs_world_add_t(world, entity, RunnerCompB);
    ecs_world_add_t(world, entity, RunnerCompC);
    ecs_world_flush(world);

    ecs_run_sync(runner);

    EcsIterator* itr = ecs_view_at(ecs_world_view_t(world, ReadA), entity);
    check_eq_int(ecs_view_read_t(itr, RunnerCompA)->f1, 819);

    ecs_run_sync(runner);

    ecs_view_itr_reset(itr);
    ecs_view_jump(itr, entity);
    check_eq_int(ecs_view_read_t(itr, RunnerCompA)->f1, 174652);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
