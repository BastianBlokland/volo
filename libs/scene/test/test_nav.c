#include "asset_register.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_math.h"
#include "ecs_runner.h"
#include "ecs_utils.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_transform.h"

ecs_view_define(LocomotionView) { ecs_access_read(SceneLocomotionComp); }
ecs_view_define(PathView) { ecs_access_read(SceneNavPathComp); }
ecs_view_define(EnvView) { ecs_access_write(SceneNavEnvComp); }

static EcsEntityId test_create_agent(EcsWorld* world, const GeoVector pos, const GeoVector target) {
  const EcsEntityId global = ecs_world_global(world);
  SceneNavEnvComp*  env    = ecs_utils_write_t(world, EnvView, global, SceneNavEnvComp);

  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = geo_quat_ident);
  ecs_world_add_t(world, e, SceneLocomotionComp, .maxSpeed = 0.0f, .radius = 0.5f);
  SceneNavAgentComp* agent = scene_nav_add_agent(world, env, e, SceneNavLayer_Normal);
  scene_nav_travel_to(agent, target);
  return e;
}

static EcsEntityId test_create_blocker(EcsWorld* world, const GeoVector pos) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = geo_quat_ident);
  scene_collision_add_sphere(world, e, (SceneCollisionSphere){.radius = .25f}, SceneLayer_Debug);
  scene_nav_add_blocker(world, e, SceneNavBlockerMask_All);
  return e;
}

static void test_check_path(
    CheckTestContext*       _testCtx,
    const SceneNavPathComp* comp,
    const GeoNavCell        expected[],
    const u32               expectedCount) {
  check_require_msg(
      comp->cellCount == expectedCount,
      "path[{}] == path[{}]",
      fmt_int(comp->cellCount),
      fmt_int(expectedCount));
  for (u32 i = 0; i != expectedCount; ++i) {
    check_msg(
        comp->cells[i].x == expected[i].x && comp->cells[i].y == expected[i].y,
        "[{}] {}x{} == {}x{}",
        fmt_int(i),
        fmt_int(comp->cells[i].x),
        fmt_int(comp->cells[i].y),
        fmt_int(expected[i].x),
        fmt_int(expected[i].y));
  }
}

ecs_module_init(nav_test_module) {
  ecs_register_view(LocomotionView);
  ecs_register_view(PathView);
  ecs_register_view(EnvView);
}

spec(nav) {

  const f32  halfGridSize = 200;
  const f32  gridDensity  = 1.25f;
  const f32  gridCellSize = 1.0f / gridDensity;
  EcsDef*    def          = null;
  EcsWorld*  world        = null;
  EcsRunner* runner       = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    scene_register(def);
    ecs_register_module(def, nav_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
    ecs_run_sync(runner);
  }

  /**
   * TODO: Disabled as this test is sensitive to changes in grid size which makes it annoying to
   * maintain.
   */
  skip_it("can compute a path around an obstacle") {
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
     *  0000
     * 011110
     * 010x11
     *  0  0
     */

    const SceneNavPathComp* path       = ecs_utils_read_t(world, PathView, agent, SceneNavPathComp);
    const u16               centerCell = (u16)math_round_nearest_f32(halfGridSize * gridDensity);
    test_check_path(
        _testCtx,
        path,
        (GeoNavCell[]){
            {.x = centerCell - 2, .y = centerCell},
            {.x = centerCell - 2, .y = centerCell + 1},
            {.x = centerCell - 1, .y = centerCell + 1},
            {.x = centerCell + 0, .y = centerCell + 1},
            {.x = centerCell + 1, .y = centerCell + 1},
            {.x = centerCell + 1, .y = centerCell},
            {.x = centerCell + 2, .y = centerCell},
        },
        7);

    const SceneNavEnvComp* env          = ecs_utils_read_t(world, EnvView, global, SceneNavEnvComp);
    const u32*             navGridStats = scene_nav_grid_stats(env, SceneNavLayer_Normal);
    (void)navGridStats;
    // TODO: The exact frame the path is queried is not deterministic atm so we cannot assert this.
    // check_eq_int(navGridStats[GeoNavStat_PathCount], 1);
    // check_eq_int(navGridStats[GeoNavStat_PathItrCells], 7);
    // check_eq_int(navGridStats[GeoNavStat_PathOutputCells], 7);
    // check_eq_int(navGridStats[GeoNavStat_PathItrEnqueues], 16);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
