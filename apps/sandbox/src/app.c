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
#include "scene_prefab.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_spawner.h"
#include "scene_terrain.h"
#include "scene_transform.h"
#include "scene_weapon.h"
#include "ui_register.h"
#include "vfx_register.h"

#include "cmd_internal.h"

static const GapVector g_appWindowSize = {1920, 1080};
static const u32       g_appPropCount  = 350;
static const u64       g_appRngSeed    = 42;

static void app_window_create(EcsWorld* world) {
  const EcsEntityId window = gap_window_create(world, GapWindowFlags_Default, g_appWindowSize);
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
      .position = {50, 75, 0},
      .rotation = geo_quat_mul(
          geo_quat_forward_to_left, geo_quat_angle_axis(geo_right, 70 * math_deg_to_rad)));
}

static void app_window_fullscreen_toggle(GapWindowComp* win) {
  const bool isFullscreen = gap_window_mode(win) == GapWindowMode_Fullscreen;
  gap_window_resize(
      win,
      isFullscreen ? gap_window_param(win, GapParam_WindowSizePreFullscreen) : gap_vector(0, 0),
      isFullscreen ? GapWindowMode_Windowed : GapWindowMode_Fullscreen);
}

static void app_scene_create_props(EcsWorld* world, Rng* rng) {
  struct {
    StringHash prefabId;
    f32        weight;
  } g_props[] = {
      {string_hash_lit("PropFence"), .weight = 0.6f},
      {string_hash_lit("PropBarrel"), .weight = 0.05f},
      {string_hash_lit("PropTree"), .weight = 0.05f},
      {string_hash_lit("PropPlant"), .weight = 0.3f},
  };

  for (u32 instIdx = 0; instIdx != g_appPropCount; ++instIdx) {
    /**
     * Pick a random prop.
     * NOTE: Weights need to be normalized.
     */
    f32        sample   = rng_sample_f32(rng);
    StringHash prefabId = g_props[array_elems(g_props) - 1].prefabId;
    for (u32 propIdx = 0; propIdx < (array_elems(g_props) - 1); ++propIdx) {
      if (sample < g_props[propIdx].weight) {
        prefabId = g_props[propIdx].prefabId;
        break;
      }
      sample -= g_props[propIdx].weight;
    }

    const f32 posX  = rng_sample_range(rng, -100.0f, 100.0f);
    const f32 posY  = rng_sample_range(rng, -0.1f, 0.1f);
    const f32 posZ  = rng_sample_range(rng, -100.0f, 100.0f);
    const f32 angle = rng_sample_f32(rng) * math_pi_f32 * 2;
    scene_prefab_spawn(
        world,
        &(ScenePrefabSpec){
            .prefabId = prefabId,
            .faction  = SceneFaction_None,
            .position = geo_vector(posX, posY, posZ),
            .rotation = geo_quat_angle_axis(geo_up, angle),
            .flags    = ScenePrefabFlags_SnapToTerrain,
        });
  }
}

static void app_scene_create_units(EcsWorld* world) {
  scene_prefab_spawn(
      world,
      &(ScenePrefabSpec){
          .prefabId = string_hash_lit("SpawnerUnitRifle"),
          .faction  = SceneFaction_A,
          .position = geo_vector(50),
          .rotation = geo_quat_ident,
          .flags    = ScenePrefabFlags_SnapToTerrain,
      });

  static const GeoVector g_turretGunLocations[] = {
      {30, 0, -15},
      {30, 0, 0},
      {30, 0, 15},
  };
  array_for_t(g_turretGunLocations, GeoVector, turretLoc) {
    scene_prefab_spawn(
        world,
        &(ScenePrefabSpec){
            .prefabId = string_hash_lit("TurretGun"),
            .faction  = SceneFaction_A,
            .position = *turretLoc,
            .rotation = geo_quat_forward_to_left,
            .flags    = ScenePrefabFlags_SnapToTerrain,
        });
  }

  static const GeoVector g_turretMissileLocations[] = {
      {40, 0, -10},
      {40, 0, 10},
  };
  array_for_t(g_turretMissileLocations, GeoVector, turretLoc) {
    scene_prefab_spawn(
        world,
        &(ScenePrefabSpec){
            .prefabId = string_hash_lit("TurretMissile"),
            .faction  = SceneFaction_A,
            .position = *turretLoc,
            .rotation = geo_quat_forward_to_left,
            .flags    = ScenePrefabFlags_SnapToTerrain,
        });
  }

  scene_prefab_spawn(
      world,
      &(ScenePrefabSpec){
          .prefabId = string_hash_lit("SpawnerUnitMelee"),
          .faction  = SceneFaction_B,
          .position = geo_vector(-50),
          .rotation = geo_quat_ident,
          .flags    = ScenePrefabFlags_SnapToTerrain,
      });
}

ecs_comp_define(AppComp) {
  bool sceneCreated;
  Rng* rng;
};

static void ecs_destruct_app_comp(void* data) {
  AppComp* comp = data;
  rng_destroy(comp->rng);
}

ecs_view_define(AppUpdateGlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(AppComp);
}

ecs_view_define(WindowView) { ecs_access_write(GapWindowComp); }
ecs_view_define(InstanceView) { ecs_access_with(ScenePrefabInstanceComp); }

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, AppUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AppComp*                app   = ecs_view_write_t(globalItr, AppComp);
  const InputManagerComp* input = ecs_view_read_t(globalItr, InputManagerComp);

  // Create the scene.
  if (!app->sceneCreated) {
    app_scene_create_props(world, app->rng);
    app_scene_create_units(world);
    app->sceneCreated = true;
  }

  if (input_triggered_lit(input, "Reset")) {
    // Destroy all instances.
    EcsView* instanceView = ecs_world_view_t(world, InstanceView);
    for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
    // Recreate the scene next frame.
    app->sceneCreated = false;
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
  ecs_register_comp(AppComp, .destructor = ecs_destruct_app_comp);

  ecs_register_view(AppUpdateGlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(InstanceView);

  ecs_register_system(
      AppUpdateSys,
      ecs_view_id(AppUpdateGlobalView),
      ecs_view_id(WindowView),
      ecs_view_id(InstanceView));
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
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  ecs_world_add_t(
      world,
      ecs_world_global(world),
      AppComp,
      .rng = rng_create_xorwow(g_alloc_heap, g_appRngSeed));

  const String assetPath = cli_read_string(invoc, g_assetFlag, string_empty);
  asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  input_resource_init(world, string_lit("global/sandbox.imp"));
  scene_prefab_init(world, string_lit("global/sandbox.pfb"));
  scene_weapon_init(world, string_lit("global/sandbox.wea"));
  scene_terrain_init(
      world,
      string_lit("graphics/scene/terrain.gra"),
      string_lit("external/terrain/terrain_3_height.r16"));

  app_window_create(world);
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowView); }
