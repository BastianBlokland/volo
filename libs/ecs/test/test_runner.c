#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "ecs_def.h"
#include "ecs_runner.h"
#include "ecs_world.h"

// TODO: We should avoid global state like this, as it prevents running the test multiple times in
// the same process.
static i64 test_sys1_counter, test_sys2_counter;

ecs_system_define(TestSys1) {
  diag_assert(g_ecsRunningSystem);
  diag_assert(ecs_world_busy(_world));

  thread_atomic_add_i64(&test_sys1_counter, 1);
}

ecs_system_define(TestSys2) {
  diag_assert(g_ecsRunningSystem);
  diag_assert(ecs_world_busy(_world));

  thread_atomic_add_i64(&test_sys2_counter, 1);
}

ecs_module_init(runner_test_module) {
  ecs_register_system(TestSys1);
  ecs_register_system(TestSys2);
}

spec(runner) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, runner_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world);
  }

  it("executes every system once") {
    check(!g_ecsRunningSystem);
    check(!ecs_world_busy(world));

    ecs_run_sync(runner);
    check_eq_int(thread_atomic_load_i64(&test_sys1_counter), 1);
    check_eq_int(thread_atomic_load_i64(&test_sys2_counter), 1);

    ecs_run_sync(runner);
    check_eq_int(thread_atomic_load_i64(&test_sys1_counter), 2);
    check_eq_int(thread_atomic_load_i64(&test_sys2_counter), 2);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
