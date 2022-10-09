#include "app_ecs.h"
#include "asset.h"
#include "core_alloc.h"
#include "core_math.h"
#include "core_rng.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "input_resource.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "ui_register.h"
#include "vfx_register.h"

#include "cmd_internal.h"
#include "object_internal.h"

typedef struct {
  GeoBox       spawnArea;
  TimeDuration spawnIntervalMin, spawnIntervalMax;
} AppFactionConfig;

static const GapVector        g_windowSize      = {1920, 1080};
static const u32              g_wallCount       = 32;
static const AppFactionConfig g_factionConfig[] = {
    {
        .spawnArea        = {.min = {.x = 30, .z = -30}, .max = {.x = 35, .z = 30}},
        .spawnIntervalMin = time_milliseconds(500),
        .spawnIntervalMax = time_milliseconds(1000),
    },
    {
        .spawnArea        = {.min = {.x = -30, .z = -30}, .max = {.x = -35, .z = 30}},
        .spawnIntervalMin = time_milliseconds(500),
        .spawnIntervalMax = time_milliseconds(1000),
    },
};

static void app_window_create(EcsWorld* world) {
  const EcsEntityId window = gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  debug_menu_create(world, window);

  ecs_world_add_t(
      world,
      window,
      SceneCameraComp,
      .persFov   = 50 * math_deg_to_rad,
      .persNear  = 0.75f,
      .orthoSize = 5);

  ecs_world_add_t(
      world,
      window,
      SceneTransformComp,
      .position = {0, 60.0f, -25.0f},
      .rotation = geo_quat_angle_axis(geo_right, 60 * math_deg_to_rad));
}

static void app_window_fullscreen_toggle(GapWindowComp* win) {
  const bool isFullscreen = gap_window_mode(win) == GapWindowMode_Fullscreen;
  gap_window_resize(
      win,
      isFullscreen ? gap_window_param(win, GapParam_WindowSizePreFullscreen) : gap_vector(0, 0),
      isFullscreen ? GapWindowMode_Windowed : GapWindowMode_Fullscreen);
}

static void app_scene_create_sky(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      entity,
      SceneRenderableComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/scene/sky.gra")));
  ecs_world_add_t(world, entity, SceneTagComp, .tags = SceneTags_Background);
}

static void app_scene_create_walls(EcsWorld* world, const ObjectDatabaseComp* objDb) {
  static const u64 g_rngSeed = 42;
  Rng*             rng       = rng_create_xorwow(g_alloc_heap, g_rngSeed);
  for (u32 i = 0; i != g_wallCount; ++i) {
    const f32     posX  = rng_sample_range(rng, -15.0f, 15.0f);
    const f32     posY  = rng_sample_range(rng, -0.1f, 0.1f);
    const f32     posZ  = rng_sample_range(rng, -40.0f, 40.0f);
    const f32     angle = rng_sample_f32(rng) * math_pi_f32 * 2;
    const GeoQuat rot   = geo_quat_angle_axis(geo_up, angle);
    object_spawn_wall(world, objDb, geo_vector(posX, posY, posZ), rot);
  }
  rng_destroy(rng);
}

static TimeDuration app_next_spawn_time(const u8 faction, const TimeDuration now) {
  TimeDuration       next        = now;
  const TimeDuration intervalMin = g_factionConfig[faction].spawnIntervalMin;
  const TimeDuration intervalMax = g_factionConfig[faction].spawnIntervalMax;
  next += (TimeDuration)rng_sample_range(g_rng, intervalMin, intervalMax);
  return next;
}

static GeoVector app_next_spawn_pos(const u8 faction) {
  const GeoBox* b = &g_factionConfig[faction].spawnArea;
  return geo_vector(
          .x = rng_sample_range(g_rng, b->min.x, b->max.x),
          .z = rng_sample_range(g_rng, b->min.z, b->max.z));
}

typedef struct {
  TimeDuration nextSpawnTime;
} AppFactionData;

ecs_comp_define(AppComp) {
  bool           sceneCreated;
  AppFactionData factionData[array_elems(g_factionConfig)];
};

ecs_view_define(AppUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(ObjectDatabaseComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(AppComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(WindowView) { ecs_access_write(GapWindowComp); }

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, AppUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AppComp*                  app    = ecs_view_write_t(globalItr, AppComp);
  AssetManagerComp*         assets = ecs_view_write_t(globalItr, AssetManagerComp);
  const InputManagerComp*   input  = ecs_view_read_t(globalItr, InputManagerComp);
  const ObjectDatabaseComp* objDb  = ecs_view_read_t(globalItr, ObjectDatabaseComp);
  const SceneTimeComp*      time   = ecs_view_read_t(globalItr, SceneTimeComp);

  // Create the inital scene.
  if (!app->sceneCreated) {
    app_scene_create_sky(world, assets);
    app_scene_create_walls(world, objDb);
    app->sceneCreated = true;
  }

  // Spawn new units.
  for (u8 faction = 0; faction != array_elems(g_factionConfig); ++faction) {
    AppFactionData* factionData = &app->factionData[faction];

    if (time->time > factionData->nextSpawnTime) {
      object_spawn_unit(world, objDb, app_next_spawn_pos(faction), faction);
      factionData->nextSpawnTime = app_next_spawn_time(faction, time->time);
    }
  }

  if (input_triggered_lit(input, "WindowNew")) {
    app_window_create(world);
  }

  EcsView*     windowView      = ecs_world_view_t(world, WindowView);
  EcsIterator* activeWindowItr = ecs_view_maybe_at(windowView, input_active_window(input));
  if (activeWindowItr) {
    GapWindowComp* win = ecs_view_write_t(activeWindowItr, GapWindowComp);
    if (input_triggered_lit(input, "WindowClose")) {
      gap_window_close(win);
    }
    if (input_triggered_lit(input, "WindowFullscreen")) {
      app_window_fullscreen_toggle(win);
    }
  }
}

ecs_module_init(sandbox_app_module) {
  ecs_register_comp(AppComp);

  ecs_register_view(AppUpdateGlobalView);
  ecs_register_view(WindowView);

  ecs_register_system(AppUpdateSys, ecs_view_id(AppUpdateGlobalView), ecs_view_id(WindowView));
}

static CliId g_assetFlag;

void app_ecs_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo Sandbox Application"));

  g_assetFlag = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Required);
  cli_register_desc(app, g_assetFlag, string_lit("Path to asset directory."));
}

void app_ecs_register(EcsDef* def, MAYBE_UNUSED const CliInvocation* invoc) {
  asset_register(def);
  debug_register(def);
  gap_register(def);
  input_register(def);
  rend_register(def);
  scene_register(def);
  ui_register(def);
  vfx_register(def);

  ecs_register_module(def, sandbox_app_module);
  ecs_register_module(def, sandbox_cmd_module);
  ecs_register_module(def, sandbox_input_module);
  ecs_register_module(def, sandbox_object_module);
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  ecs_world_add_t(world, ecs_world_global(world), AppComp);

  const String assetPath = cli_read_string(invoc, g_assetFlag, string_empty);
  asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  input_resource_create(world, string_lit("input/sandbox.imp"));

  app_window_create(world);
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowView); }
