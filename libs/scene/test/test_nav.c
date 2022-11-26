#include "asset_register.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "ecs.h"
#include "ecs_utils.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_transform.h"

static EcsEntityId test_create_agent(EcsWorld* world, const GeoVector pos, const GeoVector target) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = geo_quat_ident);
  ecs_world_add_t(world, e, SceneLocomotionComp, .maxSpeed = 0.0f, .radius = 0.5f);
  SceneNavAgentComp* agent = scene_nav_add_agent(world, e);
  scene_nav_move_to(agent, target);
  return e;
}

static EcsEntityId test_create_blocker(EcsWorld* world, const GeoVector pos) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = geo_quat_ident);
  scene_collision_add_sphere(world, e, (SceneCollisionSphere){.radius = .25f}, SceneLayer_Debug);
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

ecs_view_define(LocomotionView) { ecs_access_read(SceneLocomotionComp); }
ecs_view_define(PathView) { ecs_access_read(SceneNavPathComp); }
ecs_view_define(StatsView) { ecs_access_read(SceneNavStatsComp); }

ecs_module_init(nav_test_module) {
  ecs_register_view(LocomotionView);
  ecs_register_view(PathView);
  ecs_register_view(StatsView);
}

spec(nav) {

  const f32  halfGridSize = 100;
  const f32  gridDensity  = 1.25f;
  const f32  gridCellSize = 1.0f / gridDensity;
  EcsDef*    def          = null;
  EcsWorld*  world        = null;
  EcsRunner* runner       = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    scene_register(def);
    ecs_register_module(def, nav_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
    ecs_run_sync(runner);
  }

  it("sets the locomotion target to the destination") {
    const EcsEntityId global = ecs_world_global(world);
    const EcsEntityId agent  = test_create_agent(
        world, geo_vector(gridCellSize * -2.0f, 0, 0), geo_vector(gridCellSize * 2.0f, 0, 0));
    ecs_run_sync(runner); // Tick to create the agent.
    ecs_run_sync(runner); // Tick to execute the navigation.

    // No need to compute a path in case there's no obstacle.
    const SceneNavPathComp* path = ecs_utils_read_t(world, PathView, agent, SceneNavPathComp);
    check_eq_int(path->cellCount, 0);

    const SceneLocomotionComp* loco =
        ecs_utils_read_t(world, LocomotionView, agent, SceneLocomotionComp);
    check(loco->flags & SceneLocomotion_Moving);
    check_eq_float(loco->targetPos.x, gridCellSize * 2.0f, 1e-6f);
    check_eq_float(loco->targetPos.y, 0, 1e-6f);
    check_eq_float(loco->targetPos.z, 0, 1e-6f);

    const SceneNavStatsComp* stats = ecs_utils_read_t(world, StatsView, global, SceneNavStatsComp);
    check_eq_int(stats->gridStats[GeoNavStat_PathCount], 0);
    check_eq_int(stats->gridStats[GeoNavStat_LineQueryCount], 1);
  }

  it("can compute a path around an obstacle") {
    const EcsEntityId global = ecs_world_global(world);
    const EcsEntityId agent  = test_create_agent(
        world, geo_vector(gridCellSize * -2.0f, 0, 0), geo_vector(gridCellSize * 2.0f, 0, 0));
    test_create_blocker(world, geo_vector(0, 0, 0));
    ecs_run_sync(runner); // Tick to create the agent and the blocker.
    ecs_run_sync(runner); // Tick to register the blocker.
    ecs_run_sync(runner); // Tick to execute the navigation.

    /**
     * Verify the path.
     * Expected: (1 is an output cell, x is blocked and 0 is an enqueued neighbor).
     *
     *  00 0
     * 011x11
     *  01110
     *   000
     */

    const SceneNavPathComp* path       = ecs_utils_read_t(world, PathView, agent, SceneNavPathComp);
    const u16               centerCell = (u16)(halfGridSize * gridDensity);
    test_check_path(
        _testCtx,
        path,
        (GeoNavCell[]){
            {.x = centerCell - 2, .y = centerCell},
            {.x = centerCell - 1, .y = centerCell},
            {.x = centerCell - 1, .y = centerCell - 1},
            {.x = centerCell + 0, .y = centerCell - 1},
            {.x = centerCell + 1, .y = centerCell - 1},
            {.x = centerCell + 1, .y = centerCell},
            {.x = centerCell + 2, .y = centerCell},
        },
        7);

    const SceneNavStatsComp* stats = ecs_utils_read_t(world, StatsView, global, SceneNavStatsComp);
    (void)stats;
    // TODO: The exact frame the path is queried is not deterministic atm so we cannot assert this.
    // check_eq_int(stats->gridStats[GeoNavStat_PathCount], 1);
    // check_eq_int(stats->gridStats[GeoNavStat_PathItrCells], 7);
    // check_eq_int(stats->gridStats[GeoNavStat_PathOutputCells], 7);
    // check_eq_int(stats->gridStats[GeoNavStat_PathItrEnqueues], 16);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
