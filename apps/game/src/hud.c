#include "core_alloc.h"
#include "core_bitset.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_status.h"
#include "scene_transform.h"
#include "scene_visibility.h"
#include "ui.h"

#include "cmd_internal.h"
#include "hud_internal.h"
#include "input_internal.h"

static const f32      g_hudHealthBarOffsetY = 10.0f;
static const UiVector g_hudHealthBarSize    = {.x = 50.0f, .y = 7.5f};

static const Unicode g_hudStatusIcons[SceneStatusType_Count] = {
    [SceneStatusType_Burning] = UiShape_Whatshot,
};
static const UiColor g_hudStatusIconColors[SceneStatusType_Count] = {
    [SceneStatusType_Burning] = {.r = 255, .g = 128, .b = 0, .a = 255},
};
static const u8 g_hudStatusIconOutline[SceneStatusType_Count] = {
    [SceneStatusType_Burning] = 2,
};
static const UiVector g_hudStatusIconSize = {.x = 15.0f, .y = 15.0f};
static const UiVector g_hudStatusSpacing  = {.x = 2.0f, .y = 4.0f};

ecs_comp_define(HudComp) { EcsEntityId uiCanvas; };

ecs_view_define(GlobalView) { ecs_access_write(CmdControllerComp); }

ecs_view_define(HudView) {
  ecs_access_read(HudComp);
  ecs_access_read(InputStateComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

ecs_view_define(UiCanvasView) { ecs_access_write(UiCanvasComp); }

ecs_view_define(HealthView) {
  ecs_access_maybe_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneStatusComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneHealthComp);
  ecs_access_read(SceneTransformComp);
}

ecs_view_define(InfoView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneHealthComp);
  ecs_access_maybe_read(SceneLocomotionComp);
  ecs_access_maybe_read(SceneStatusComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneNameComp);
}

static GeoMatrix hud_ui_view_proj(
    const SceneCameraComp* cam, const SceneTransformComp* camTrans, const UiCanvasComp* canvas) {
  const UiVector res    = ui_canvas_resolution(canvas);
  const f32      aspect = (f32)res.width / (f32)res.height;
  return scene_camera_view_proj(cam, camTrans, aspect);
}

static GeoVector hud_world_to_ui_pos(const GeoMatrix* viewProj, const GeoVector pos) {
  const GeoVector ndcPos = geo_matrix_transform(viewProj, geo_vector(pos.x, pos.y, pos.z, 1));
  if (UNLIKELY(ndcPos.w == 0)) {
    return geo_vector(-1, -1, -1, -1); // Not a valid position on screen.
  }
  const GeoVector persDivPos = geo_vector_perspective_div(ndcPos);
  const GeoVector normPos    = geo_vector_mul(geo_vector_add(persDivPos, geo_vector(1, 1)), 0.5f);
  return geo_vector(normPos.x, 1.0f - normPos.y, persDivPos.z);
}

static GeoVector hud_entity_world_pos_top(
    const SceneTransformComp* trans,
    const SceneScaleComp*     scale,
    const SceneCollisionComp* collision) {
  if (collision) {
    const GeoBox worldBounds = scene_collision_world_bounds(collision, trans, scale);
    return geo_vector(
        (worldBounds.min.x + worldBounds.max.x) * 0.5f,
        worldBounds.max.y,
        (worldBounds.min.z + worldBounds.max.z) * 0.5f);
  }
  return trans->position;
}

static UiColor hud_health_color(const f32 norm) {
  static const UiColor g_colorFull = {8, 255, 8, 192};
  static const UiColor g_colorWarn = {255, 255, 8, 192};
  static const UiColor g_colorDead = {255, 8, 8, 192};
  if (norm < 0.5f) {
    return ui_color_lerp(g_colorDead, g_colorWarn, norm * 0.5f);
  }
  return ui_color_lerp(g_colorWarn, g_colorFull, (norm - 0.5f) * 2.0f);
}

static void hud_health_draw(UiCanvasComp* canvas, const GeoMatrix* viewProj, EcsView* healthView) {
  ui_style_push(canvas);
  for (EcsIterator* itr = ecs_view_itr(healthView); ecs_view_walk(itr);) {
    const SceneHealthComp*    health    = ecs_view_read_t(itr, SceneHealthComp);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale     = ecs_view_read_t(itr, SceneScaleComp);
    const SceneCollisionComp* collision = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneStatusComp*    status    = ecs_view_read_t(itr, SceneStatusComp);

    if (health->norm <= f32_epsilon || (health->norm > 0.999f && !(status && status->active))) {
      continue; // Hide health-bars if entity is death or at full health without any status-effects.
    }

    const SceneVisibilityComp* visComp = ecs_view_read_t(itr, SceneVisibilityComp);
    if (visComp && !scene_visible(visComp, SceneFaction_A)) {
      continue; // TODO: Make the local faction configurable instead of hardcoding 'A'.
    }

    const GeoVector worldPos  = hud_entity_world_pos_top(trans, scale, collision);
    const GeoVector canvasPos = hud_world_to_ui_pos(viewProj, worldPos);
    if (UNLIKELY(canvasPos.z <= 0)) {
      continue; // Position is behind the camera.
    }

    // Compute the health-bar ui rectangle.
    ui_layout_set_pos(canvas, UiBase_Canvas, ui_vector(canvasPos.x, canvasPos.y), UiBase_Canvas);
    ui_layout_move_dir(canvas, Ui_Up, g_hudHealthBarOffsetY, UiBase_Absolute);
    ui_layout_resize(canvas, UiAlign_MiddleCenter, g_hudHealthBarSize, UiBase_Absolute, Ui_XY);

    // Draw the health-bar background.
    ui_style_color(canvas, ui_color(8, 8, 8, 192));
    ui_canvas_draw_glyph(canvas, UiShape_Circle, 4, UiFlags_None);

    // Draw the health-bar foreground.
    ui_style_color(canvas, hud_health_color(health->norm));
    ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(health->norm, 0), UiBase_Current, Ui_X);
    ui_canvas_draw_glyph(canvas, UiShape_Circle, 4, UiFlags_None);

    if (status && status->active) {
      ui_layout_next(canvas, Ui_Up, g_hudStatusSpacing.y);
      ui_layout_resize(canvas, UiAlign_BottomLeft, g_hudStatusIconSize, UiBase_Absolute, Ui_XY);
      bitset_for(bitset_from_var(status->active), typeIndex) {
        ui_style_color(canvas, g_hudStatusIconColors[typeIndex]);
        ui_style_outline(canvas, g_hudStatusIconOutline[typeIndex]);
        ui_canvas_draw_glyph(canvas, g_hudStatusIcons[typeIndex], 0, UiFlags_None);
        ui_layout_next(canvas, Ui_Right, g_hudStatusSpacing.x);
      }
    }
  }
  ui_style_pop(canvas);
  ui_canvas_id_block_next(canvas); // End on an consistent id.
}

static void hud_groups_draw(UiCanvasComp* canvas, CmdControllerComp* cmd) {
  static const UiVector g_size    = {50, 25};
  static const f32      g_spacing = 8.0f;

  ui_layout_move_to(canvas, UiBase_Container, UiAlign_BottomLeft, Ui_XY);
  ui_layout_move(canvas, ui_vector(g_spacing, g_spacing), UiBase_Absolute, Ui_XY);
  ui_layout_resize(canvas, UiAlign_BottomLeft, g_size, UiBase_Absolute, Ui_XY);

  for (u32 i = cmd_group_count; i-- != 0;) {
    const u32 size = cmd_group_size(cmd, i);
    if (!size) {
      continue;
    }
    if (ui_button(
            canvas,
            .label      = fmt_write_scratch("\a|02{}\ar {}", fmt_int(i + 1), fmt_ui_shape(Group)),
            .fontSize   = 20,
            .frameColor = ui_color(32, 32, 32, 192),
            .tooltip    = fmt_write_scratch("Size: {}", fmt_int(size)))) {
      cmd_push_select_group(cmd, i);
    }
    ui_layout_next(canvas, Ui_Up, g_spacing);
  }
}

static String hud_info_faction_name(const SceneFaction faction) {
  switch (faction) {
  case SceneFaction_A:
    return string_lit("Player");
  default:
    return string_lit("Enemy");
  }
}

static void hud_info_draw(UiCanvasComp* canvas, EcsIterator* infoItr) {
  const SceneFactionComp*    factionComp = ecs_view_read_t(infoItr, SceneFactionComp);
  const SceneHealthComp*     healthComp  = ecs_view_read_t(infoItr, SceneHealthComp);
  const SceneLocomotionComp* locoComp    = ecs_view_read_t(infoItr, SceneLocomotionComp);
  const SceneNameComp*       nameComp    = ecs_view_read_t(infoItr, SceneNameComp);
  const SceneStatusComp*     statusComp  = ecs_view_read_t(infoItr, SceneStatusComp);
  const SceneVisibilityComp* visComp     = ecs_view_read_t(infoItr, SceneVisibilityComp);

  if (visComp && !scene_visible(visComp, SceneFaction_A)) {
    return; // TODO: Make the local faction configurable instead of hardcoding 'A'.
  }

  const String entityName = stringtable_lookup(g_stringtable, nameComp->name);

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, 4 * usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  fmt_write(&buffer, "\a.bName\ar:\a>10{}\n", fmt_text(entityName));
  if (factionComp) {
    const String factionName = hud_info_faction_name(factionComp->id);
    fmt_write(&buffer, "\a.bFaction\ar:\a>10{}\n", fmt_text(factionName));
  }
  if (healthComp) {
    const u32 healthVal    = (u32)math_round_up_f32(healthComp->max * healthComp->norm);
    const u32 healthMaxVal = (u32)math_round_up_f32(healthComp->max);
    fmt_write(&buffer, "\a.bHealth\ar:\a>10{} / {}\n", fmt_int(healthVal), fmt_int(healthMaxVal));
  }
  if (statusComp && statusComp->active) {
    fmt_write(&buffer, "\a.bStatus\ar:\a>10");
    bool first = true;
    bitset_for(bitset_from_var(statusComp->active), typeIndex) {
      if (!first) {
        dynstring_append(&buffer, string_lit(", "));
      }
      first = false;
      fmt_write(
          &buffer,
          "\a|02{}{}\ar {}",
          fmt_ui_color(g_hudStatusIconColors[typeIndex]),
          fmt_text(ui_shape_scratch(g_hudStatusIcons[typeIndex])),
          fmt_text(scene_status_name((SceneStatusType)typeIndex)));
    }
    dynstring_append_char(&buffer, '\n');
  }
  if (locoComp) {
    fmt_write(&buffer, "\a.bSpeed\ar:\a>10{}\n", fmt_float(locoComp->maxSpeed, .maxDecDigits = 1));
  }

  ui_tooltip(canvas, sentinel_u64, dynstring_view(&buffer));
}

ecs_system_define(HudDrawUiSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  CmdControllerComp* cmd = ecs_view_write_t(globalItr, CmdControllerComp);

  EcsView* hudView    = ecs_world_view_t(world, HudView);
  EcsView* canvasView = ecs_world_view_t(world, UiCanvasView);
  EcsView* healthView = ecs_world_view_t(world, HealthView);
  EcsView* infoView   = ecs_world_view_t(world, InfoView);

  EcsIterator* canvasItr = ecs_view_itr(canvasView);
  EcsIterator* infoItr   = ecs_view_itr(infoView);

  for (EcsIterator* itr = ecs_view_itr(hudView); ecs_view_walk(itr);) {
    const HudComp*            state      = ecs_view_read_t(itr, HudComp);
    const InputStateComp*     inputState = ecs_view_read_t(itr, InputStateComp);
    const SceneCameraComp*    cam        = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* camTrans   = ecs_view_read_t(itr, SceneTransformComp);
    if (!ecs_view_maybe_jump(canvasItr, state->uiCanvas)) {
      continue;
    }
    UiCanvasComp*   canvas   = ecs_view_write_t(canvasItr, UiCanvasComp);
    const GeoMatrix viewProj = hud_ui_view_proj(cam, camTrans, canvas);

    ui_canvas_reset(canvas);
    ui_canvas_to_back(canvas);

    hud_health_draw(canvas, &viewProj, healthView);
    hud_groups_draw(canvas, cmd);

    const EcsEntityId  hoveredEntity = input_hovered_entity(inputState);
    const TimeDuration hoveredTime   = input_hovered_time(inputState);
    if (hoveredTime >= time_second && ecs_view_maybe_jump(infoItr, hoveredEntity)) {
      hud_info_draw(canvas, infoItr);
    }
    ui_canvas_id_block_next(canvas); // End on an consistent id.
  }
}

ecs_module_init(game_hud_module) {
  ecs_register_comp(HudComp);

  ecs_register_view(GlobalView);
  ecs_register_view(HudView);
  ecs_register_view(UiCanvasView);
  ecs_register_view(HealthView);
  ecs_register_view(InfoView);

  ecs_register_system(
      HudDrawUiSys,
      ecs_view_id(GlobalView),
      ecs_view_id(HudView),
      ecs_view_id(UiCanvasView),
      ecs_view_id(HealthView),
      ecs_view_id(InfoView));

  enum {
    Order_Normal    = 0,
    Order_HudDrawUi = 1,
  };
  ecs_order(HudDrawUiSys, Order_HudDrawUi);
}

void hud_init(EcsWorld* world, const EcsEntityId cameraEntity) {
  ecs_world_add_t(
      world,
      cameraEntity,
      HudComp,
      .uiCanvas = ui_canvas_create(world, cameraEntity, UiCanvasCreateFlags_None));
}
