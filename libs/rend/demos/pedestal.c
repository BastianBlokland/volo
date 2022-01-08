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
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_velocity.h"

/**
 * Demo application that renders a single subject.
 */

static const GapVector g_windowSize          = {1024, 768};
static const f32       g_statSmoothFactor    = 0.05f;
static const f32       g_cameraFov           = 60.0f * math_deg_to_rad;
static const f32       g_cameraNearPlane     = 0.1f;
static const GeoVector g_cameraPosition      = {0, 1.5f, -3.0f};
static const f32       g_cameraAngle         = 10 * math_deg_to_rad;
static const f32       g_cameraMoveSpeed     = 10.0f;
static const GeoVector g_pedestalPosition    = {0, 0.5f, 0};
static const f32       g_pedestalRotateSpeed = 45.0f * math_deg_to_rad;
static const GeoVector g_subjectPosition     = {0, 1.0f, 0};
static const String    g_subjectGraphics[]   = {
    string_static("graphics/cube.gra"),
    string_static("graphics/sphere.gra"),
    string_static("graphics/demo_bunny.gra"),
    string_static("graphics/demo_cayo.gra"),
    string_static("graphics/demo_corset.gra"),
    string_static("graphics/demo_head.gra"),
    string_static("graphics/demo_head_wire.gra"),
};

static f32 demo_smooth_f32(const f32 old, const f32 new) {
  return old + ((new - old) * g_statSmoothFactor);
}

static TimeDuration demo_smooth_duration(const TimeDuration old, const TimeDuration new) {
  return (TimeDuration)((f64)old + ((f64)(new - old) * g_statSmoothFactor));
}

static EcsEntityId demo_add_object(
    EcsWorld* world, AssetManagerComp* assets, const GeoVector position, const String graphic) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(world, entity, RendInstanceComp, .graphic = asset_lookup(world, assets, graphic));
  ecs_world_add_t(
      world, entity, SceneTransformComp, .position = position, .rotation = geo_quat_ident);
  ecs_world_add_t(
      world, entity, SceneVelocityComp, .angularVelocity = geo_vector(0, g_pedestalRotateSpeed, 0));
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

  f32          updateFreq;
  TimeDuration renderTime;
};

ecs_view_define(UpdateGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(DemoComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(UpdateWindowView) {
  ecs_access_write(GapWindowComp);
  ecs_access_maybe_read(RendStatsComp);
}

ecs_system_define(DemoUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DemoComp*            demo   = ecs_view_write_t(globalItr, DemoComp);
  AssetManagerComp*    assets = ecs_view_write_t(globalItr, AssetManagerComp);
  const SceneTimeComp* time   = ecs_view_read_t(globalItr, SceneTimeComp);
  if (!demo->initialized) {
    log_i("Initializing demo");

    demo_add_sky(world, assets);
    demo_add_grid(world, assets);
    demo_add_object(world, assets, g_pedestalPosition, string_lit("graphics/demo_pedestal.gra"));
    demo->subject     = demo_add_object(world, assets, g_subjectPosition, g_subjectGraphics[0]);
    demo->initialized = true;
  }

  EcsIterator*   windowItr   = ecs_view_at(ecs_world_view_t(world, UpdateWindowView), demo->window);
  GapWindowComp* window      = ecs_view_write_t(windowItr, GapWindowComp);
  const RendStatsComp* stats = ecs_view_read_t(windowItr, RendStatsComp);

  const f32 deltaSeconds = time->delta / (f32)time_second;
  demo->updateFreq       = demo_smooth_f32(demo->updateFreq, 1.0f / deltaSeconds);
  if (stats) {
    demo->renderTime = demo_smooth_duration(demo->renderTime, stats->renderTime);
  }

  // Update window title.
  if ((time->ticks % 4) == 0) {
    gap_window_title_set(
        window,
        fmt_write_scratch(
            "{>4} hz | {>8} gpu | {>8} ram | {>8} vram",
            fmt_float(demo->updateFreq, .maxDecDigits = 0),
            fmt_duration(demo->renderTime),
            fmt_size(alloc_stats_total()),
            fmt_size(stats ? stats->vramOccupied : 0)));
  }

  // Change subject on input.
  if (gap_window_key_pressed(window, GapKey_Space)) {
    log_i("Changing subject", log_param("index", fmt_int(demo->subjectIndex)));

    ecs_world_entity_destroy(world, demo->subject);
    demo->subjectIndex = (demo->subjectIndex + 1) % array_elems(g_subjectGraphics);
    demo->subject =
        demo_add_object(world, assets, g_subjectPosition, g_subjectGraphics[demo->subjectIndex]);
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
