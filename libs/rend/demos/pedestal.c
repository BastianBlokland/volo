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

/**
 * Demo application that renders a subject graphic.
 */

static const GapVector g_windowSize          = {1024, 768};
static const f32       g_statSmoothFactor    = 0.05f;
static const u32       g_titleUpdateInterval = 4;
static const f32       g_cameraFov           = 60.0f * math_deg_to_rad;
static const f32       g_cameraNearPlane     = 0.1f;
static const GeoVector g_cameraPosition      = {0, 1.5f, -3.0f};
static const f32       g_cameraAngle         = 10 * math_deg_to_rad;
static const f32       g_cameraMoveSpeed     = 10.0f;
static const f32       g_pedestalRotateSpeed = 45.0f * math_deg_to_rad;
static const f32       g_pedestalPositionY   = 0.5f;
static const f32       g_subjectPositionY    = 1.0f;
static const f32       g_subjectSpacing      = 2.5f;
static const String    g_subjectGraphics[]   = {
    string_static("graphics/cube.gra"),
    string_static("graphics/sphere.gra"),
    string_static("graphics/demo_bunny.gra"),
    string_static("graphics/demo_cayo.gra"),
    string_static("graphics/demo_corset.gra"),
    string_static("graphics/demo_head.gra"),
    string_static("graphics/demo_head_wire.gra"),
};

typedef enum {
  DemoFlags_Initialized = 1 << 0,
  DemoFlags_Dirty       = 1 << 1,
  DemoFlags_Rotate      = 1 << 2,
} DemoFlags;

ecs_comp_define(DemoComp) {
  DemoFlags   flags;
  EcsEntityId window;
  u32         subjectCount;
  u32         subjectIndex;

  f32          updateFreq;
  TimeDuration renderTime;
};

ecs_comp_define(DemoObjectComp);

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(DemoComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(WindowView) {
  ecs_access_write(GapWindowComp);
  ecs_access_maybe_read(RendStatsComp);
}

ecs_view_define(ObjectView) {
  ecs_access_with(DemoObjectComp);
  ecs_access_write(SceneTransformComp);
}

static f32 demo_smooth_f32(const f32 old, const f32 new) {
  return old + ((new - old) * g_statSmoothFactor);
}

static TimeDuration demo_smooth_duration(const TimeDuration old, const TimeDuration new) {
  return (TimeDuration)((f64)old + ((f64)(new - old) * g_statSmoothFactor));
}

static void demo_spawn_sky(EcsWorld* world, AssetManagerComp* assets) {
  ecs_world_add_t(
      world,
      ecs_world_entity_create(world),
      RendInstanceComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/sky.gra")));
}

static void demo_spawn_grid(EcsWorld* world, AssetManagerComp* assets) {
  ecs_world_add_t(
      world,
      ecs_world_entity_create(world),
      RendInstanceComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/grid.gra")));
}

static void demo_spawn_object(
    EcsWorld* world, AssetManagerComp* assets, const GeoVector position, const String graphic) {

  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, RendInstanceComp, .graphic = asset_lookup(world, assets, graphic));
  ecs_world_add_t(world, e, SceneTransformComp, .position = position, .rotation = geo_quat_ident);
  ecs_world_add_empty_t(world, e, DemoObjectComp);
}

static void demo_spawn_objects(EcsWorld* world, DemoComp* demo, AssetManagerComp* assets) {
  const u32 columnCount = (u32)math_sqrt_f32(demo->subjectCount);
  const u32 rowCount    = columnCount;

  for (u32 x = 0; x != columnCount; ++x) {
    for (u32 y = 0; y != rowCount; ++y) {

      const GeoVector gridPos = geo_vector(
          (x - (columnCount - 1) * 0.5f) * g_subjectSpacing,
          (y - (rowCount - 1) * 0.5f) * g_subjectSpacing);

      demo_spawn_object(
          world,
          assets,
          geo_vector(gridPos.x, g_subjectPositionY, gridPos.y),
          g_subjectGraphics[demo->subjectIndex]);

      demo_spawn_object(
          world,
          assets,
          geo_vector(gridPos.x, g_pedestalPositionY, gridPos.y),
          string_lit("graphics/demo_pedestal.gra"));
    }
  }
}

static EcsEntityId demo_window_open(EcsWorld* world) {
  const EcsEntityId e = gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  ecs_world_add_t(world, e, SceneCameraComp, .fov = g_cameraFov, .zNear = g_cameraNearPlane);
  ecs_world_add_t(world, e, SceneCameraMovementComp, .moveSpeed = g_cameraMoveSpeed);
  ecs_world_add_t(
      world,
      e,
      SceneTransformComp,
      .position = g_cameraPosition,
      .rotation = geo_quat_angle_axis(geo_right, g_cameraAngle));
  return e;
}

static void demo_window_title_set(GapWindowComp* win, DemoComp* demo, const RendStatsComp* stats) {
  gap_window_title_set(
      win,
      fmt_write_scratch(
          "{>4} hz | {>8} gpu | {>6} kverts | {>6} ktris | {>8} ram | {>8} vram | {>7} rend-ram",
          fmt_float(demo->updateFreq, .maxDecDigits = 0),
          fmt_duration(demo->renderTime),
          fmt_int(stats ? stats->vertices / 1000 : 0),
          fmt_int(stats ? stats->primitives / 1000 : 0),
          fmt_size(alloc_stats_total()),
          fmt_size(stats ? stats->vramOccupied : 0),
          fmt_size(stats ? stats->ramOccupied : 0)));
}

ecs_system_define(DemoUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DemoComp*            demo   = ecs_view_write_t(globalItr, DemoComp);
  AssetManagerComp*    assets = ecs_view_write_t(globalItr, AssetManagerComp);
  const SceneTimeComp* time   = ecs_view_read_t(globalItr, SceneTimeComp);
  if (!(demo->flags & DemoFlags_Initialized)) {
    demo_spawn_sky(world, assets);
    demo_spawn_grid(world, assets);
    demo->flags |= DemoFlags_Initialized | DemoFlags_Rotate | DemoFlags_Dirty;
  }

  EcsIterator*         windowItr = ecs_view_at(ecs_world_view_t(world, WindowView), demo->window);
  GapWindowComp*       window    = ecs_view_write_t(windowItr, GapWindowComp);
  const RendStatsComp* rendStats = ecs_view_read_t(windowItr, RendStatsComp);

  const f32 deltaSeconds = time->delta / (f32)time_second;
  demo->updateFreq       = demo_smooth_f32(demo->updateFreq, 1.0f / deltaSeconds);
  if (rendStats) {
    demo->renderTime = demo_smooth_duration(demo->renderTime, rendStats->renderTime);
  }

  if ((time->ticks % g_titleUpdateInterval) == 0) {
    demo_window_title_set(window, demo, rendStats);
  }

  if (gap_window_key_pressed(window, GapKey_Space)) {
    demo->subjectIndex = (demo->subjectIndex + 1) % array_elems(g_subjectGraphics);
    demo->flags |= DemoFlags_Dirty;
  }

  if (gap_window_key_pressed(window, GapKey_Return)) {
    demo->flags ^= DemoFlags_Rotate;
  }

  if (gap_window_key_pressed(window, GapKey_Alpha1)) {
    demo->subjectCount = 1;
    demo->flags |= DemoFlags_Dirty;
  }
  if (gap_window_key_pressed(window, GapKey_Alpha2)) {
    demo->subjectCount = 64;
    demo->flags |= DemoFlags_Dirty;
  }
  if (gap_window_key_pressed(window, GapKey_Alpha3)) {
    demo->subjectCount = 512;
    demo->flags |= DemoFlags_Dirty;
  }
  if (gap_window_key_pressed(window, GapKey_Alpha4)) {
    demo->subjectCount = 1024;
    demo->flags |= DemoFlags_Dirty;
  }
  if (gap_window_key_pressed(window, GapKey_Alpha5)) {
    demo->subjectCount = 4096;
    demo->flags |= DemoFlags_Dirty;
  }
  if (gap_window_key_pressed(window, GapKey_Alpha0)) {
    demo->subjectCount = 0;
    demo->flags |= DemoFlags_Dirty;
  }

  if (demo->flags & DemoFlags_Dirty) {
    EcsView* objectView = ecs_world_view_t(world, ObjectView);
    for (EcsIterator* objItr = ecs_view_itr(objectView); ecs_view_walk(objItr);) {
      ecs_world_entity_destroy(world, ecs_view_entity(objItr));
    }
    demo_spawn_objects(world, demo, assets);
    demo->flags &= ~DemoFlags_Dirty;
  }
}

ecs_system_define(DemoSetRotationSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const DemoComp*      demo        = ecs_view_read_t(globalItr, DemoComp);
  const SceneTimeComp* time        = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            timeSeconds = time->time / (f32)time_second;

  if (!(demo->flags & DemoFlags_Rotate)) {
    return;
  }

  EcsView* objectView = ecs_world_view_t(world, ObjectView);
  for (EcsIterator* objItr = ecs_view_itr(objectView); ecs_view_walk(objItr);) {
    SceneTransformComp* transComp = ecs_view_write_t(objItr, SceneTransformComp);
    transComp->rotation = geo_quat_angle_axis(geo_up, timeSeconds * g_pedestalRotateSpeed);
  }
}

ecs_module_init(demo_cube_module) {
  ecs_register_comp(DemoComp);
  ecs_register_comp_empty(DemoObjectComp);

  ecs_register_view(GlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(ObjectView);

  ecs_register_system(
      DemoUpdateSys, ecs_view_id(GlobalView), ecs_view_id(WindowView), ecs_view_id(ObjectView));
  ecs_register_system(DemoSetRotationSys, ecs_view_id(GlobalView), ecs_view_id(ObjectView));
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
  ecs_register_module(def, demo_cube_module);
  rend_register(def);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  asset_manager_create_fs(world, AssetManagerFlags_TrackChanges, assetPath);

  const EcsEntityId window = demo_window_open(world);
  ecs_world_add_t(world, ecs_world_global(world), DemoComp, .window = window, .subjectCount = 1);

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
