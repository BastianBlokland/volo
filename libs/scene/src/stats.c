#include "core_alloc.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "scene_camera.h"
#include "scene_lifetime.h"
#include "scene_stats.h"
#include "scene_text.h"
#include "scene_time.h"

static const f32 g_sceneStatsUiPadding    = 5.0f;
static const f32 g_sceneStatsUiTextSize   = 25.0f;
static const f32 g_sceneStatsSmoothFactor = 0.1f;

ecs_comp_define_public(SceneStatsCamComp);

typedef enum {
  SceneStatsUi_Enabled = 1 << 0,
} SceneStatsUiFlags;

ecs_comp_define(SceneStatsUiComp) {
  SceneStatsUiFlags flags;
  TimeDuration      updateTime, renderTime;
  EcsEntityId       text;
};

static void ecs_destruct_rend_stats_comp(void* data) {
  SceneStatsCamComp* comp = data;
  if (!string_is_empty(comp->gpuName)) {
    string_free(g_alloc_heap, comp->gpuName);
  }
}

static TimeDuration stats_smooth_duration(const TimeDuration old, const TimeDuration new) {
  return (TimeDuration)((f64)old + ((f64)(new - old) * g_sceneStatsSmoothFactor));
}

static EcsEntityId
stats_create_text(EcsWorld* world, const SceneCameraComp* cam, const EcsEntityId owner) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  SceneTextComp*    text   = scene_text_add(world, entity);
  scene_text_update_size(text, g_sceneStatsUiTextSize);
  scene_text_update_palette(text, TextPalette_B, geo_color_yellow);
  scene_text_update_palette(text, TextPalette_C, geo_color_red);

  scene_tag_add(world, entity, cam->filter.required);
  ecs_world_add_t(world, entity, SceneLifetimeOwnerComp, .owner = owner);
  return entity;
}

static void stats_ui_text_dur(
    DynString*         str,
    const TimeDuration dur,
    const String       label,
    const TimeDuration threshold1,
    const TimeDuration threshold2) {
  const TextPalette valPalette =
      dur > threshold2 ? TextPalette_C : (dur > threshold1 ? TextPalette_B : TextPalette_A);
  fmt_write(
      str,
      "\ue9e4 {}{<9} {}{}",
      fmt_text_palette(valPalette),
      fmt_duration(dur),
      fmt_text_palette(TextPalette_A),
      fmt_text(label));
  const f32 freq = 1.0f / (dur / (f32)time_second);
  fmt_write(
      str,
      " ({}{}{} hz)\n",
      fmt_text_palette(valPalette),
      fmt_float(freq, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_text_palette(TextPalette_A));
}

static String stats_ui_text(const SceneStatsUiComp* ui, const SceneStatsCamComp* camStats) {
  DynString str = dynstring_create(g_alloc_scratch, usize_kibibyte);

  // clang-format off
  fmt_write(&str, "\ue80e {}\n", fmt_text(camStats->gpuName));
  fmt_write(&str, "\uef5b {<4}x{<4} pixels\n", fmt_int(camStats->renderSize[0]), fmt_int(camStats->renderSize[1]));
  stats_ui_text_dur(&str, ui->updateTime, string_lit("update time"), time_milliseconds(17), time_milliseconds(17));
  stats_ui_text_dur(&str, ui->renderTime, string_lit("render time"), time_milliseconds(10), time_milliseconds(17));
  fmt_write(&str, "\ue3ae {<9} draws\n", fmt_int(camStats->draws));
  fmt_write(&str, "\ue4fc {<9} instances\n", fmt_int(camStats->instances));
  fmt_write(&str, "\ue4fc {<9} vertices\n", fmt_int(camStats->vertices));
  fmt_write(&str, "\ue4fc {<9} triangles\n", fmt_int(camStats->primitives));
  fmt_write(&str, "\uea5f {<9} vertex shaders\n", fmt_int(camStats->shadersVert));
  fmt_write(&str, "\uea5f {<9} fragment shaders\n", fmt_int(camStats->shadersFrag));
  fmt_write(&str, "\ue1db {<9} memory-main\n", fmt_size(alloc_stats_total()));
  fmt_write(&str, "\ue1db {<9} memory-renderer (reserved: {})\n", fmt_size(camStats->ramOccupied), fmt_size(camStats->ramReserved));
  fmt_write(&str, "\ue1db {<9} memory-gpu (reserved: {})\n", fmt_size(camStats->vramOccupied), fmt_size(camStats->vramReserved));
  fmt_write(&str, "\ue338 {<9} descriptor-sets (reserved: {})\n", fmt_int(camStats->descSetsOccupied), fmt_int(camStats->descSetsReserved));
  fmt_write(&str, "\ue338 {<9} descriptor-layouts\n", fmt_int(camStats->descLayouts));
  fmt_write(&str, "\ue069 {<9} graphics\n", fmt_int(camStats->resources[SceneStatRes_Graphic]));
  fmt_write(&str, "\ue069 {<9} shaders\n", fmt_int(camStats->resources[SceneStatRes_Shader]));
  fmt_write(&str, "\ue069 {<9} meshes\n", fmt_int(camStats->resources[SceneStatRes_Mesh]));
  fmt_write(&str, "\ue069 {<9} textures\n", fmt_int(camStats->resources[SceneStatRes_Texture]));
  // clang-format on

  return dynstring_view(&str);
}

ecs_view_define(UiGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(UiCreateView) {
  ecs_access_read(SceneCameraComp);
  ecs_access_without(SceneStatsUiComp);
}

ecs_view_define(UiUpdateView) {
  ecs_access_read(GapWindowComp);
  ecs_access_read(SceneStatsCamComp);
  ecs_access_write(SceneStatsUiComp);
}

ecs_view_define(UiTextView) { ecs_access_write(SceneTextComp); }

ecs_system_define(SceneStatsUiCreateSys) {
  EcsView* createView = ecs_world_view_t(world, UiCreateView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    const EcsEntityId      entity = ecs_view_entity(itr);
    const SceneCameraComp* cam    = ecs_view_read_t(itr, SceneCameraComp);

    ecs_world_add_t(
        world,
        entity,
        SceneStatsUiComp,
        .flags = SceneStatsUi_Enabled,
        .text  = stats_create_text(world, cam, entity));
    ecs_world_add_t(world, entity, SceneStatsCamComp);
  }
}

ecs_system_define(SceneStatsUiUpdateSys) {
  EcsView*             globalView = ecs_world_view_t(world, UiGlobalView);
  EcsIterator*         globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  const SceneTimeComp* time       = globalItr ? ecs_view_read_t(globalItr, SceneTimeComp) : null;

  EcsView*     textView = ecs_world_view_t(world, UiTextView);
  EcsIterator* textItr  = ecs_view_itr(textView);

  EcsView* updateView = ecs_world_view_t(world, UiUpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const GapWindowComp*     win      = ecs_view_read_t(itr, GapWindowComp);
    const SceneStatsCamComp* camStats = ecs_view_read_t(itr, SceneStatsCamComp);
    SceneStatsUiComp*        ui       = ecs_view_write_t(itr, SceneStatsUiComp);

    if (gap_window_key_pressed(win, GapKey_F5)) {
      ui->flags ^= SceneStatsUi_Enabled;
    }

    ui->updateTime = stats_smooth_duration(ui->updateTime, time ? time->delta : time_second);
    ui->renderTime = stats_smooth_duration(ui->renderTime, camStats->renderTime);

    ecs_view_jump(textItr, ui->text);
    SceneTextComp* text = ecs_view_write_t(textItr, SceneTextComp);

    if (ui->flags & SceneStatsUi_Enabled) {
      const f32 windowHeight = gap_window_param(win, GapParam_WindowSize).y;
      scene_text_update_position(
          text,
          g_sceneStatsUiPadding,
          windowHeight - g_sceneStatsUiTextSize - g_sceneStatsUiPadding);
      scene_text_update_str(text, stats_ui_text(ui, camStats));
    } else {
      scene_text_update_str(text, string_empty);
    }
  }
}

ecs_module_init(scene_stats_module) {
  ecs_register_comp(SceneStatsCamComp, .destructor = ecs_destruct_rend_stats_comp);
  ecs_register_comp(SceneStatsUiComp);

  ecs_register_view(UiGlobalView);
  ecs_register_view(UiCreateView);
  ecs_register_view(UiUpdateView);
  ecs_register_view(UiTextView);

  ecs_register_system(SceneStatsUiCreateSys, ecs_view_id(UiCreateView));
  ecs_register_system(
      SceneStatsUiUpdateSys,
      ecs_view_id(UiGlobalView),
      ecs_view_id(UiUpdateView),
      ecs_view_id(UiTextView));
}
