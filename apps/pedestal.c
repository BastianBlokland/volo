#include "asset.h"
#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "core_math.h"
#include "core_thread.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "jobs.h"
#include "log.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_selection.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "ui.h"

/**
 * Demo application that renders a subject graphic.
 */

typedef struct {
  String                graphic;
  GeoVector             position;
  f32                   scale;
  SceneCollisionCapsule collisionCapsule;
} Subject;

static const GapVector g_windowSize          = {1920, 1080};
static const f32       g_pedestalRotateSpeed = 25.0f * math_deg_to_rad;
static const f32       g_pedestalPositionY   = 0.5f;
static const f32       g_subjectSpacing      = 2.5f;
static const Subject   g_subjects[]          = {
               {
                   .graphic          = string_static("graphics/demo/vanguard.gra"),
                   .position         = {.y = 0.5f},
                   .scale            = 1.0f,
                   .collisionCapsule = {.offset = {0, 0.3f, 0}, .radius = 0.3f, .height = 1.2f},
    },
               {
                   .graphic          = string_static("graphics/cube.gra"),
                   .position         = {.y = 1},
                   .scale            = 1.0f,
                   .collisionCapsule = {.radius = 0.65f},
    },
               {
                   .graphic          = string_static("graphics/sphere.gra"),
                   .position         = {.y = 1},
                   .scale            = 1.0f,
                   .collisionCapsule = {.radius = 0.5f},
    },
               {
                   .graphic  = string_static("graphics/demo/normal_tangent_mirror_test.gra"),
                   .position = {.y = 1.25f},
                   .scale    = 0.5f,
                   .collisionCapsule =
                       {.offset = {-0.6f, 0, 0}, .dir = SceneCollision_Right, .radius = 0.75f, .height = 1.2f},
    },
               {
                   .graphic          = string_static("graphics/demo/suzanne.gra"),
                   .position         = {.y = 1.25f},
                   .scale            = 0.5f,
                   .collisionCapsule = {.radius = 1},
    },
               {
                   .graphic  = string_static("graphics/demo/rigged-simple.gra"),
                   .position = {.y = 1.0f},
                   .scale    = 0.25f,
                   .collisionCapsule =
                       {.offset = {0, 0, -6}, .dir = SceneCollision_Forward, .radius = 1, .height = 6},
    },
               {
                   .graphic  = string_static("graphics/demo/fox.gra"),
                   .position = {.y = 0.5f},
                   .scale    = 0.015f,
                   .collisionCapsule =
                       {.offset = {0, 40, -35}, .dir = SceneCollision_Forward, .radius = 25, .height = 80},
    },
               {
                   .graphic          = string_static("graphics/demo/terrain.gra"),
                   .position         = {.y = 0.5f},
                   .scale            = 1.5f,
                   .collisionCapsule = {.radius = 0.5f},
    },
               {
                   .graphic          = string_static("graphics/demo/bunny.gra"),
                   .position         = {.y = 0.45f},
                   .scale            = 0.75f,
                   .collisionCapsule = {.offset = {-0.15f, 0.75f, 0.15f}, .radius = 0.75f},
    },
               {
                   .graphic  = string_static("graphics/demo/dragon.gra"),
                   .position = {.y = 1.05f},
                   .scale    = 2.0f,
                   .collisionCapsule =
                       {.offset = {0, 0, -0.1f},
                        .dir    = SceneCollision_Forward,
                        .radius = 0.3f,
                        .height = 0.25f},
    },
               {
                   .graphic          = string_static("graphics/demo/cayo.gra"),
                   .position         = {.y = 0.5f},
                   .scale            = 0.8f,
                   .collisionCapsule = {.offset = {0, 0.4f, 0}, .radius = 0.4f, .height = 1.2f},
    },
               {
                   .graphic          = string_static("graphics/demo/corset.gra"),
                   .position         = {.y = 0.5f},
                   .scale            = 30.0f,
                   .collisionCapsule = {.offset = {0, 0.01f, 0.003f}, .radius = 0.012f, .height = 0.04f},
    },
               {
                   .graphic          = string_static("graphics/demo/boombox.gra"),
                   .position         = {.y = 0.95f},
                   .scale            = 50.0f,
                   .collisionCapsule = {.radius = 0.01f},
    },
               {
                   .graphic          = string_static("graphics/demo/head.gra"),
                   .position         = {.y = 1.3f},
                   .scale            = 3.0f,
                   .collisionCapsule = {.offset = {0, -0.1f, -0.1f}, .radius = 0.2f, .height = 0.1f},
    },
};

typedef enum {
  AppFlags_Dirty = 1 << 0,

  AppFlags_Init = AppFlags_Dirty,
} AppFlags;

ecs_comp_define(AppComp) {
  AppFlags flags;
  u32      subjectCount;
  u32      subjectIndex;
};

ecs_comp_define(SubjectComp);

ecs_view_define(GlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(AppComp);
  ecs_access_write(SceneSelectionComp);
}

ecs_view_define(WindowView) { ecs_access_write(GapWindowComp); }

ecs_view_define(CameraView) {
  ecs_access_write(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(ObjectView) {
  ecs_access_with(SubjectComp);
  ecs_access_write(SceneTransformComp);
}

static EcsEntityId spawn_object(
    EcsWorld*         world,
    AssetManagerComp* assets,
    const GeoVector   position,
    const String      graphic,
    const f32         scale) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = asset_lookup(world, assets, graphic));
  ecs_world_add_t(world, e, SceneTransformComp, .position = position, .rotation = geo_quat_ident);
  ecs_world_add_t(world, e, SceneScaleComp, .scale = scale);
  ecs_world_add_empty_t(world, e, SubjectComp);
  return e;
}

static void spawn_objects(EcsWorld* world, AppComp* app, AssetManagerComp* assets) {
  const u32 columnCount = (u32)math_sqrt_f32(app->subjectCount);
  const u32 rowCount    = columnCount;

  for (u32 x = 0; x != columnCount; ++x) {
    for (u32 y = 0; y != rowCount; ++y) {

      const GeoVector gridPos = geo_vector(
          (x - (columnCount - 1) * 0.5f) * g_subjectSpacing,
          (y - (rowCount - 1) * 0.5f) * g_subjectSpacing);

      const EcsEntityId e = spawn_object(
          world,
          assets,
          geo_vector_add(
              g_subjects[app->subjectIndex].position, geo_vector(gridPos.x, 0, gridPos.y)),
          g_subjects[app->subjectIndex].graphic,
          g_subjects[app->subjectIndex].scale);

      scene_collision_add_capsule(world, e, g_subjects[app->subjectIndex].collisionCapsule);

      spawn_object(
          world,
          assets,
          geo_vector_add(
              geo_vector(gridPos.x, g_pedestalPositionY, gridPos.y), geo_vector(0, -0.8f)),
          string_lit("graphics/demo/pedestal.gra"),
          0.4f);
    }
  }
}

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*            assets       = ecs_view_write_t(globalItr, AssetManagerComp);
  AppComp*                     app          = ecs_view_write_t(globalItr, AppComp);
  const InputManagerComp*      input        = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  SceneSelectionComp*          selection    = ecs_view_write_t(globalItr, SceneSelectionComp);

  if (input_triggered_lit(input, "PedestalPrev")) {
    app->subjectIndex = (app->subjectIndex + 1) % array_elems(g_subjects);
    app->flags |= AppFlags_Dirty;
  }
  if (input_triggered_lit(input, "PedestalNext")) {
    app->subjectIndex = (app->subjectIndex ? app->subjectIndex : array_elems(g_subjects)) - 1;
    app->flags |= AppFlags_Dirty;
  }
  if (input_triggered_lit(input, "PedestalSetInstCount0")) {
    app->subjectCount = 0;
    app->flags |= AppFlags_Dirty;
  }
  if (input_triggered_lit(input, "PedestalSetInstCount1")) {
    app->subjectCount = 1;
    app->flags |= AppFlags_Dirty;
  }
  if (input_triggered_lit(input, "PedestalSetInstCount64")) {
    app->subjectCount = 64;
    app->flags |= AppFlags_Dirty;
  }
  if (input_triggered_lit(input, "PedestalSetInstCount512")) {
    app->subjectCount = 512;
    app->flags |= AppFlags_Dirty;
  }
  if (input_triggered_lit(input, "PedestalSetInstCount1024")) {
    app->subjectCount = 1024;
    app->flags |= AppFlags_Dirty;
  }
  if (input_triggered_lit(input, "PedestalSetInstCount4096")) {
    app->subjectCount = 4096;
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

  const EcsEntityId activeWindow = input_active_window(input);
  EcsIterator*      camItr = ecs_view_maybe_at(ecs_world_view_t(world, CameraView), activeWindow);
  if (camItr && input_triggered_lit(input, "PedestalSelect")) {
    const SceneCameraComp*    cam     = ecs_view_read_t(camItr, SceneCameraComp);
    const SceneTransformComp* trans   = ecs_view_read_t(camItr, SceneTransformComp);
    const GeoVector           normPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
    const GeoRay ray = scene_camera_ray(cam, trans, input_cursor_aspect(input), normPos);

    SceneRayHit hit;
    if (scene_query_ray(collisionEnv, &ray, &hit) && hit.entity != scene_selected(selection)) {
      scene_select(selection, hit.entity);
    } else {
      scene_deselect(selection);
    }
  }
}

ecs_system_define(AppSetRotationSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const f32 timeSeconds = scene_time_seconds(ecs_view_read_t(globalItr, SceneTimeComp));

  const GeoQuat newRot     = geo_quat_angle_axis(geo_up, timeSeconds * g_pedestalRotateSpeed);
  EcsView*      objectView = ecs_world_view_t(world, ObjectView);
  for (EcsIterator* objItr = ecs_view_itr(objectView); ecs_view_walk(objItr);) {
    ecs_view_write_t(objItr, SceneTransformComp)->rotation = newRot;
  }
}

ecs_module_init(app_pedestal_module) {
  ecs_register_comp(AppComp);
  ecs_register_comp_empty(SubjectComp);

  ecs_register_view(GlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(CameraView);
  ecs_register_view(ObjectView);

  ecs_register_system(
      AppUpdateSys, ecs_view_id(GlobalView), ecs_view_id(CameraView), ecs_view_id(ObjectView));
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
  debug_register(def);
  gap_register(def);
  input_register(def);
  rend_register(def);
  scene_register(def);
  ui_register(def);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  const EcsEntityId window = gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  debug_menu_create(world, window);

  ecs_world_add_t(
      world, ecs_world_global(world), AppComp, .flags = AppFlags_Init, .subjectCount = 0);

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
