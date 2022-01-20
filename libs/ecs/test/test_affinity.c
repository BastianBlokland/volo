#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "ecs_def.h"
#include "ecs_runner.h"
#include "ecs_world.h"

ecs_comp_define(AffinityComp) { i64 tid; };

ecs_view_define(Write) { ecs_access_write(AffinityComp); }

ecs_system_define(AffinitySys) {
  EcsView* view = ecs_world_view_t(world, Write);
  for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr);) {
    AffinityComp* comp = ecs_view_write_t(itr, AffinityComp);

    if (sentinel_check(comp->tid)) {
      comp->tid = g_thread_tid;
      continue;
    }
    diag_assert_msg(comp->tid == g_thread_tid, "Affinity system was executed on multiple threads");
  }
}

ecs_module_init(affinity_test_module) {
  ecs_register_comp(AffinityComp);
  ecs_register_view(Write);
  ecs_register_system_with_flags(AffinitySys, EcsSystemFlags_ThreadAffinity, ecs_view_id(Write));
}

spec(affinity) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, affinity_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("executes systems with thread affinity always on the same thread") {
    static const usize g_numTicks = 100;

    const EcsEntityId entity = ecs_world_entity_create(world);
    ecs_world_add_t(world, entity, AffinityComp, .tid = sentinel_i64);
    ecs_world_flush(world);

    for (usize i = 0; i != g_numTicks; ++i) {
      ecs_run_sync(runner);
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
