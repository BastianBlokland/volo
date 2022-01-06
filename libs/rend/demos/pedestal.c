#include "asset.h"
#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "core_math.h"
#include "core_thread.h"
#include "ecs.h"
#include "gap.h"
#include "geo.h"
#include "jobs.h"
#include "log.h"
#include "rend.h"
#include "scene_camera.h"
#include "scene_register.h"
#include "scene_transform.h"
#include "scene_velocity.h"

/**
 * Demo application that renders a single subject.
 */

static const GapVector g_windowSize         = {1024, 768};
static const f32       g_cameraFov          = 60.0f * math_deg_to_rad;
static const f32       g_cameraNearPlane    = 0.1f;
static const GeoVector g_cameraPosition     = {0, 1.1f, -2.5f};
static const f32       g_cameraAngle        = 10 * math_deg_to_rad;
static const f32       g_cameraMoveSpeed    = 10.0f;
static const GeoVector g_subjectPosition    = {0, 0.5f, 0};
static const f32       g_subjectRotateSpeed = 45.0f * math_deg_to_rad;
static const String    g_subjectGraphics[]  = {
    string_static("graphics/cube.gra"),
    string_static("graphics/sphere.gra"),
    string_static("graphics/demo_bunny.gra"),
    string_static("graphics/demo_corset.gra"),
    string_static("graphics/demo_head.gra"),
    string_static("graphics/demo_head_wire.gra"),
};

static EcsEntityId demo_add_subject(EcsWorld* world, AssetManagerComp* assets, String graphic) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(world, entity, RendInstanceComp, .graphic = asset_lookup(world, assets, graphic));
  ecs_world_add_t(
      world, entity, SceneTransformComp, .position = g_subjectPosition, .rotation = geo_quat_ident);
  ecs_world_add_t(
      world, entity, SceneVelocityComp, .angularVelocity = geo_vector(0, g_subjectRotateSpeed, 0));
  return entity;
}

static EcsEntityId demo_add_sky(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      ecs_world_entity_create(world),
      RendInstanceComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/sky.gra")));
  return entity;
}

static EcsEntityId demo_add_grid(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      ecs_world_entity_create(world),
      RendInstanceComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/grid.gra")));
  return entity;
}

static EcsEntityId demo_open_window(EcsWorld* world) {
  const EcsEntityId entity = gap_window_create(world, GapWindowFlags_Default, g_windowSize);

  ecs_world_add_t(world, entity, SceneCameraComp, .fov = g_cameraFov, .zNear = g_cameraNearPlane);
  ecs_world_add_t(world, entity, SceneCameraMovementComp, .moveSpeed = g_cameraMoveSpeed);
  ecs_world_add_t(
      world,
      entity,
      SceneTransformComp,
      .position = g_cameraPosition,
      .rotation = geo_quat_angle_axis(geo_right, g_cameraAngle));

  return entity;
}

ecs_comp_define(DemoComp) {
  bool        initialized;
  EcsEntityId window;
  u32         subjectIndex;
  EcsEntityId subject;
};

ecs_view_define(UpdateGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(DemoComp);
}

ecs_view_define(UpdateWindowView) { ecs_access_read(GapWindowComp); }

ecs_system_define(DemoUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);
  DemoComp*         demo   = ecs_view_write_t(globalItr, DemoComp);
  if (!demo->initialized) {
    demo_add_sky(world, assets);
    demo_add_grid(world, assets);
    demo->subject     = demo_add_subject(world, assets, g_subjectGraphics[0]);
    demo->initialized = true;
  }
  const GapWindowComp* win = ecs_utils_read_t(world, UpdateWindowView, demo->window, GapWindowComp);
  if (gap_window_key_pressed(win, GapKey_Space)) {
    ecs_world_entity_destroy(world, demo->subject);
    demo->subjectIndex = (demo->subjectIndex + 1) % array_elems(g_subjectGraphics);
    demo->subject      = demo_add_subject(world, assets, g_subjectGraphics[demo->subjectIndex]);
  }
}

ecs_module_init(demo_cube_module) {
  ecs_register_comp(DemoComp);

  ecs_register_system(
      DemoUpdateSys, ecs_register_view(UpdateGlobalView), ecs_register_view(UpdateWindowView));
}

static int demo_run(const String assetPath) {
  log_i(
      "Demo startup",
      log_param("asset-path", fmt_text(assetPath)),
      log_param("pid", fmt_int(g_thread_pid)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  asset_register(def);
  scene_register(def);
  gap_register(def);
  rend_register(def);
  ecs_register_module(def, demo_cube_module);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  asset_manager_create_fs(world, AssetManagerFlags_TrackChanges, assetPath);

  const EcsEntityId window = demo_open_window(world);
  ecs_world_add_t(world, ecs_world_global(world), DemoComp, .window = window);

  while (ecs_world_exists(world, window)) {
    ecs_run_sync(runner);
  }

  log_i("Demo shutdown", log_param("mem", fmt_size(alloc_stats_total())));

  rend_teardown(world);
  ecs_runner_destroy(runner);
  ecs_world_destroy(world);
  ecs_def_destroy(def);
  return 0;
}

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  int exitCode = 0;

  CliApp* app       = cli_app_create(g_alloc_heap, string_lit("Volo Render Pedestal Demo"));
  CliId   assetFlag = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Required);
  cli_register_desc(app, assetFlag, string_lit("Path to asset directory."));

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  const String assetPath = cli_read_string(invoc, assetFlag, string_empty);
  exitCode               = demo_run(assetPath);

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
