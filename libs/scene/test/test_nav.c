#include "check_spec.h"
#include "core_alloc.h"
#include "ecs.h"
#include "ecs_utils.h"
#include "scene_collision.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_transform.h"

static EcsEntityId test_create_agent(EcsWorld* world, const GeoVector pos, const GeoVector target) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = geo_quat_ident);
  scene_nav_add_agent(world, e, target);
  return e;
}

static EcsEntityId test_create_blocker(EcsWorld* world, const GeoVector pos) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = geo_quat_ident);
  scene_collision_add_sphere(world, e, (SceneCollisionSphere){.radius = .25f});
  scene_nav_add_blocker(world, e);
  return e;
}

static void test_check_path(
    CheckTestContext*       _testCtx,
    const SceneNavPathComp* comp,
    const GeoNavCell        expected[],
    const u32               expectedCount) {
  check_eq_int(comp->cellCount, expectedCount);
  check_require(comp->cellCount == expectedCount);
  for (u32 i = 0; i != expectedCount; ++i) {
    check_eq_int(comp->cells[i].x, expected[i].x);
    check_eq_int(comp->cells[i].y, expected[i].y);
  }
}

ecs_view_define(PathView) { ecs_access_read(SceneNavPathComp); }
ecs_view_define(StatsView) { ecs_access_read(SceneNavStatsComp); }

ecs_module_init(nav_test_module) {
  ecs_register_view(PathView);
  ecs_register_view(StatsView);
}

spec(nav) {

  const u16  halfGridSize = 50;
  EcsDef*    def          = null;
  EcsWorld*  world        = null;
  EcsRunner* runner       = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    scene_register(def);
    ecs_register_module(def, nav_test_module);

    world = ecs_world_create(g_alloc_heap, def);
    ecs_world_flush(world);

    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
    ecs_run_sync(runner);
  }

  it("can compute a straight line path") {
    const EcsEntityId global = ecs_world_global(world);
    const EcsEntityId agent  = test_create_agent(world, geo_vector(-2, 0, 0), geo_vector(2, 0, 0));
    ecs_run_sync(runner); // Tick to create the agent.
    ecs_run_sync(runner); // Tick to query the path.

    /**
     * Verify the path.
     * Expected: (1 is an output cell, 0 is an enqueued neighbor).
     *
     *  0000
     * 011111
     *  0000
     */

    const SceneNavPathComp* path = ecs_utils_read_t(world, PathView, agent, SceneNavPathComp);
    test_check_path(
        _testCtx,
        path,
        (GeoNavCell[]){
            {.x = halfGridSize - 2, .y = halfGridSize},
            {.x = halfGridSize - 1, .y = halfGridSize},
            {.x = halfGridSize + 0, .y = halfGridSize},
            {.x = halfGridSize + 1, .y = halfGridSize},
            {.x = halfGridSize + 2, .y = halfGridSize},
        },
        5);

    const SceneNavStatsComp* stats = ecs_utils_read_t(world, StatsView, global, SceneNavStatsComp);
    check_eq_int(stats->gridStats[GeoNavStat_PathCount], 1);
    check_eq_int(stats->gridStats[GeoNavStat_PathItrCells], 5);
    check_eq_int(stats->gridStats[GeoNavStat_PathOutputCells], 5);
    check_eq_int(stats->gridStats[GeoNavStat_PathItrEnqueues], 14);
  }

  it("can compute a path around an obstacle") {
    const EcsEntityId global = ecs_world_global(world);
    const EcsEntityId agent  = test_create_agent(world, geo_vector(-2, 0, 0), geo_vector(2, 0, 0));
    test_create_blocker(world, geo_vector(0, 0, 0));
    ecs_run_sync(runner); // Tick to create the agent and the blocker.
    ecs_run_sync(runner); // Tick to register the blocker.
    ecs_run_sync(runner); // Tick to query the path.

    /**
     * Verify the path.
     * Expected: (1 is an output cell, x is blocked and 0 is an enqueued neighbor).
     *
     *  00 0
     * 011x11
     *  01110
     *   000
     */

    const SceneNavPathComp* path = ecs_utils_read_t(world, PathView, agent, SceneNavPathComp);
    test_check_path(
        _testCtx,
        path,
        (GeoNavCell[]){
            {.x = halfGridSize - 2, .y = halfGridSize},
            {.x = halfGridSize - 1, .y = halfGridSize},
            {.x = halfGridSize - 1, .y = halfGridSize - 1},
            {.x = halfGridSize + 0, .y = halfGridSize - 1},
            {.x = halfGridSize + 1, .y = halfGridSize - 1},
            {.x = halfGridSize + 1, .y = halfGridSize},
            {.x = halfGridSize + 2, .y = halfGridSize},
        },
        7);

    const SceneNavStatsComp* stats = ecs_utils_read_t(world, StatsView, global, SceneNavStatsComp);
    check_eq_int(stats->gridStats[GeoNavStat_PathCount], 1);
    check_eq_int(stats->gridStats[GeoNavStat_PathItrCells], 7);
    check_eq_int(stats->gridStats[GeoNavStat_PathOutputCells], 7);
    check_eq_int(stats->gridStats[GeoNavStat_PathItrEnqueues], 16);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
