#include "asset.h"
#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "core_math.h"
#include "core_thread.h"
#include "ecs.h"
#include "gap.h"
#include "jobs.h"
#include "log.h"
#include "rend.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_time.h"
#include "scene_transform.h"

/**
 * Demo application that renders a subject graphic.
 */

static const GapVector g_windowSize          = {1024, 768};
static const f32       g_statSmoothFactor    = 0.05f;
static const u32       g_titleUpdateInterval = 4;
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
  AppFlags_Dirty  = 1 << 0,
  AppFlags_Rotate = 1 << 1,

  AppFlags_Init = AppFlags_Dirty | AppFlags_Rotate,
} AppFlags;

ecs_comp_define(AppComp) {
  AppFlags     flags;
  u32          subjectCount;
  u32          subjectIndex;
  f32          updateFreq;
  TimeDuration renderTime;
};

ecs_comp_define(SubjectComp);

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(AppComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(WindowView) {
  ecs_access_write(GapWindowComp);
  ecs_access_maybe_read(RendStatsComp);
}

ecs_view_define(ObjectView) {
  ecs_access_with(SubjectComp);
  ecs_access_write(SceneTransformComp);
}

static f32 smooth_f32(const f32 old, const f32 new) {
  return old + ((new - old) * g_statSmoothFactor);
}

static TimeDuration smooth_duration(const TimeDuration old, const TimeDuration new) {
  return (TimeDuration)((f64)old + ((f64)(new - old) * g_statSmoothFactor));
}

static void spawn_object(
    EcsWorld* world, AssetManagerComp* assets, const GeoVector position, const String graphic) {

  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = asset_lookup(world, assets, graphic));
  ecs_world_add_t(world, e, SceneTransformComp, .position = position, .rotation = geo_quat_ident);
  ecs_world_add_empty_t(world, e, SubjectComp);
}

static void spawn_objects(EcsWorld* world, AppComp* app, AssetManagerComp* assets) {
  const u32 columnCount = (u32)math_sqrt_f32(app->subjectCount);
  const u32 rowCount    = columnCount;

  for (u32 x = 0; x != columnCount; ++x) {
    for (u32 y = 0; y != rowCount; ++y) {

      const GeoVector gridPos = geo_vector(
          (x - (columnCount - 1) * 0.5f) * g_subjectSpacing,
          (y - (rowCount - 1) * 0.5f) * g_subjectSpacing);

      spawn_object(
          world,
          assets,
          geo_vector(gridPos.x, g_subjectPositionY, gridPos.y),
          g_subjectGraphics[app->subjectIndex]);

      spawn_object(
          world,
          assets,
          geo_vector(gridPos.x, g_pedestalPositionY, gridPos.y),
          string_lit("graphics/demo_pedestal.gra"));
    }
  }
}

static void window_title_set(GapWindowComp* win, AppComp* app, const RendStatsComp* stats) {
  gap_window_title_set(
      win,
      fmt_write_scratch(
          "{} | {>4} hz | {>8} gpu | {>6} kverts | {>6} ktris | {>8} ram | {>8} vram | {>7} "
          "rend-ram",
          rend_size_fmt(stats ? stats->renderResolution : rend_size(0, 0)),
          fmt_float(app->updateFreq, .maxDecDigits = 0),
          fmt_duration(app->renderTime),
          fmt_int(stats ? stats->vertices / 1000 : 0),
          fmt_int(stats ? stats->primitives / 1000 : 0),
          fmt_size(alloc_stats_total()),
          fmt_size(stats ? stats->vramOccupied : 0),
          fmt_size(stats ? stats->ramOccupied : 0)));
}

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AppComp*             app    = ecs_view_write_t(globalItr, AppComp);
  AssetManagerComp*    assets = ecs_view_write_t(globalItr, AssetManagerComp);
  const SceneTimeComp* time   = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* windowView = ecs_world_view_t(world, WindowView);
  for (EcsIterator* windowItr = ecs_view_itr(windowView); ecs_view_walk(windowItr);) {
    GapWindowComp*       window    = ecs_view_write_t(windowItr, GapWindowComp);
    const RendStatsComp* rendStats = ecs_view_read_t(windowItr, RendStatsComp);

    const f32 deltaSeconds = time->delta / (f32)time_second;
    app->updateFreq        = smooth_f32(app->updateFreq, 1.0f / deltaSeconds);
    if (rendStats) {
      app->renderTime = smooth_duration(app->renderTime, rendStats->renderTime);
    }

    if ((time->ticks % g_titleUpdateInterval) == 0) {
      window_title_set(window, app, rendStats);
    }

    if (gap_window_key_pressed(window, GapKey_Space)) {
      app->subjectIndex = (app->subjectIndex + 1) % array_elems(g_subjectGraphics);
      app->flags |= AppFlags_Dirty;
    }
    if (gap_window_key_pressed(window, GapKey_Backspace)) {
      app->flags ^= AppFlags_Rotate;
    }
    if (gap_window_key_pressed(window, GapKey_Return)) {
      gap_window_create(world, GapWindowFlags_Default, g_windowSize);
    }
    if (gap_window_key_pressed(window, GapKey_Alpha1)) {
      app->subjectCount = 1;
      app->flags |= AppFlags_Dirty;
    }
    if (gap_window_key_pressed(window, GapKey_Alpha2)) {
      app->subjectCount = 64;
      app->flags |= AppFlags_Dirty;
    }
    if (gap_window_key_pressed(window, GapKey_Alpha3)) {
      app->subjectCount = 512;
      app->flags |= AppFlags_Dirty;
    }
    if (gap_window_key_pressed(window, GapKey_Alpha4)) {
      app->subjectCount = 1024;
      app->flags |= AppFlags_Dirty;
    }
    if (gap_window_key_pressed(window, GapKey_Alpha5)) {
      app->subjectCount = 4096;
      app->flags |= AppFlags_Dirty;
    }
    if (gap_window_key_pressed(window, GapKey_Alpha0)) {
      app->subjectCount = 0;
      app->flags |= AppFlags_Dirty;
    }

    if (app->flags & AppFlags_Dirty) {
      EcsView* objectView = ecs_world_view_t(world, ObjectView);
      for (EcsIterator* objItr = ecs_view_itr(objectView); ecs_view_walk(objItr);) {
        ecs_world_entity_destroy(world, ecs_view_entity(objItr));
      }
      spawn_objects(world, app, assets);
      app->flags &= ~AppFlags_Dirty;
    }
  }
}

ecs_system_define(AppSetRotationSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const AppComp*       app         = ecs_view_read_t(globalItr, AppComp);
  const SceneTimeComp* time        = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            timeSeconds = time->time / (f32)time_second;

  if (!(app->flags & AppFlags_Rotate)) {
    return;
  }

  EcsView* objectView = ecs_world_view_t(world, ObjectView);
  for (EcsIterator* objItr = ecs_view_itr(objectView); ecs_view_walk(objItr);) {
    SceneTransformComp* transComp = ecs_view_write_t(objItr, SceneTransformComp);
    transComp->rotation = geo_quat_angle_axis(geo_up, timeSeconds * g_pedestalRotateSpeed);
  }
}

ecs_module_init(app_pedestal_module) {
  ecs_register_comp(AppComp);
  ecs_register_comp_empty(SubjectComp);

  ecs_register_view(GlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(ObjectView);

  ecs_register_system(
      AppUpdateSys, ecs_view_id(GlobalView), ecs_view_id(WindowView), ecs_view_id(ObjectView));
  ecs_register_system(AppSetRotationSys, ecs_view_id(GlobalView), ecs_view_id(ObjectView));
}

static int app_run(const String assetPath) {
  log_i(
      "Application startup",
      log_param("asset-path", fmt_text(assetPath)),
      log_param("pid", fmt_int(g_thread_pid)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  ecs_register_module(def, app_pedestal_module);
  asset_register(def);
  gap_register(def);
  rend_register(def);
  scene_register(def);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  asset_manager_create_fs(world, AssetManagerFlags_TrackChanges, assetPath);

  gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  ecs_world_add_t(
      world, ecs_world_global(world), AppComp, .flags = AppFlags_Init, .subjectCount = 1);

  do {
    ecs_run_sync(runner);
  } while (ecs_utils_any(world, WindowView));

  ecs_runner_destroy(runner);
  ecs_world_destroy(world);
  ecs_def_destroy(def);

  log_i("Application shutdown");
  return 0;
}

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  int exitCode = 0;

  CliApp* app       = cli_app_create(g_alloc_heap, string_lit("Volo Pedestal Demo"));
  CliId   assetFlag = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Required);
  cli_register_desc(app, assetFlag, string_lit("Path to asset directory."));

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  const String assetPath = cli_read_string(invoc, assetFlag, string_empty);
  exitCode               = app_run(assetPath);

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
