#include "asset.h"
#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "ecs.h"
#include "gap.h"
#include "jobs.h"
#include "log.h"
#include "rend.h"
#include "scene_register.h"
#include "scene_renderable.h"

typedef enum {
  AppFlags_Init  = 1 << 0,
  AppFlags_Dirty = 1 << 1,
} AppFlags;

ecs_comp_define(AppComp) {
  AppFlags    flags;
  EcsEntityId window;
  EcsEntityId fontAsset;
  EcsEntityId lineRenderer;
  u32         unicode;
};

ecs_view_define(GlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_write(AppComp);
}

ecs_view_define(FontView) { ecs_access_read(AssetFontComp); }
ecs_view_define(LineRendererView) { ecs_access_write(SceneRenderableUniqueComp); }
ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

static void app_render_ui(
    const AssetFontComp* font, const AppComp* app, SceneRenderableUniqueComp* lineRenderer) {

  const AssetFontGlyph* glyph = asset_font_lookup_unicode(font, app->unicode);

  GeoVector* lines = scene_renderable_unique_data_alloc(lineRenderer, sizeof(GeoVector) * 512).ptr;
  u32        lineCount = 0;
  f32        offsetX   = 0.25f;
  f32        offsetY   = 0.25f;
  f32        scale     = 0.5f;

  for (usize seg = glyph->segmentIndex; seg != glyph->segmentIndex + glyph->segmentCount; ++seg) {
    switch (font->segments[seg].type) {
    case AssetFontSegment_Line: {
      const AssetFontPoint start = font->points[font->segments[seg].pointIndex + 0];
      const AssetFontPoint end   = font->points[font->segments[seg].pointIndex + 1];
      lines[lineCount++]         = geo_vector(
          offsetX + start.x * scale,
          offsetY + start.y * scale,
          offsetX + end.x * scale,
          offsetY + end.y * scale);
    } break;
    case AssetFontSegment_QuadraticBezier: {
      const AssetFontPoint start = font->points[font->segments[seg].pointIndex + 0];
      const AssetFontPoint end   = font->points[font->segments[seg].pointIndex + 2];
      lines[lineCount++]         = geo_vector(
          offsetX + start.x * scale,
          offsetY + start.y * scale,
          offsetX + end.x * scale,
          offsetY + end.y * scale);
    } break;
    }
  }
  lineRenderer->vertexCountOverride = lineCount * 2;
}

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AppComp*          app    = ecs_view_write_t(globalItr, AppComp);
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  if (app->flags & AppFlags_Init) {
    app->fontAsset = asset_lookup(world, assets, string_lit("fonts/hack_regular.ttf"));
    asset_acquire(world, app->fontAsset);

    app->lineRenderer = ecs_world_entity_create(world);
    ecs_world_add_t(
        world,
        app->lineRenderer,
        SceneRenderableUniqueComp,
        .graphic = asset_lookup(world, assets, string_lit("graphics/ui_lines.gra")));

    app->unicode = 0x42;
    app->flags &= ~AppFlags_Init;
    app->flags |= AppFlags_Dirty;
  }

  EcsView* fontView = ecs_world_view_t(world, FontView);
  if (!ecs_view_contains(fontView, app->fontAsset)) {
    return;
  }

  const GapWindowComp* win = ecs_utils_read_t(world, WindowView, app->window, GapWindowComp);
  if (gap_window_key_pressed(win, GapKey_ArrowRight)) {
    ++app->unicode;
    app->flags |= AppFlags_Dirty;
  }
  if (gap_window_key_pressed(win, GapKey_ArrowLeft)) {
    --app->unicode;
    app->flags |= AppFlags_Dirty;
  }

  if (app->flags & AppFlags_Dirty) {
    const AssetFontComp* font =
        ecs_utils_read(fontView, app->fontAsset, ecs_comp_id(AssetFontComp));
    SceneRenderableUniqueComp* lineRenderer =
        ecs_utils_write_t(world, LineRendererView, app->lineRenderer, SceneRenderableUniqueComp);

    app_render_ui(font, app, lineRenderer);
    app->flags &= ~AppFlags_Dirty;
  }
}

ecs_module_init(app_font_module) {
  ecs_register_comp(AppComp);

  ecs_register_view(GlobalView);
  ecs_register_view(FontView);
  ecs_register_view(LineRendererView);
  ecs_register_view(WindowView);

  ecs_register_system(
      AppUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(FontView),
      ecs_view_id(LineRendererView),
      ecs_view_id(WindowView));
}

static int app_run(const String assetPath) {
  log_i("Application startup", log_param("asset-path", fmt_text(assetPath)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  ecs_register_module(def, app_font_module);
  asset_register(def);
  gap_register(def);
  rend_register(def);
  scene_register(def);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, 0);

  asset_manager_create_fs(world, AssetManagerFlags_TrackChanges, assetPath);

  const EcsEntityId win = gap_window_create(world, GapWindowFlags_Default, (GapVector){1024, 768});
  ecs_world_add_t(world, ecs_world_global(world), AppComp, .flags = AppFlags_Init, .window = win);

  while (ecs_world_exists(world, win)) {
    ecs_run_sync(runner);
  }

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

  CliApp* app       = cli_app_create(g_alloc_heap, string_lit("Volo Font Demo"));
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
