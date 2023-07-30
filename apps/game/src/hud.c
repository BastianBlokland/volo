#include "asset_product.h"
#include "asset_weapon.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "input_manager.h"
#include "rend_settings.h"
#include "scene_attack.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_product.h"
#include "scene_selection.h"
#include "scene_status.h"
#include "scene_target.h"
#include "scene_terrain.h"
#include "scene_transform.h"
#include "scene_unit.h"
#include "scene_visibility.h"
#include "scene_weapon.h"
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
static const UiVector g_hudStatusIconSize   = {.x = 15.0f, .y = 15.0f};
static const UiVector g_hudStatusSpacing    = {.x = 2.0f, .y = 4.0f};
static const UiVector g_hudMinimapSize      = {.x = 300.0f, .y = 300.0f};
static const f32      g_hudMinimapPlaySize  = 225.0f;
static const f32      g_hudMinimapAlpha     = 0.95f;
static const f32      g_hudMinimapDotRadius = 2.0f;
static const f32      g_hudMinimapLineWidth = 2.5f;
static const UiVector g_hudProductionSize   = {.x = 300.0f, .y = 400.0f};

ecs_comp_define(HudComp) {
  EcsEntityId  uiCanvas;
  UiRect       minimapRect;
  UiScrollview productionScrollView;
};

ecs_view_define(GlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneWeaponResourceComp);
  ecs_access_write(CmdControllerComp);
}

ecs_view_define(HudView) {
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(HudComp);
  ecs_access_write(InputStateComp);
  ecs_access_write(RendSettingsComp);
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
  ecs_access_maybe_read(SceneAttackComp);
  ecs_access_maybe_read(SceneDamageStatsComp);
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneHealthComp);
  ecs_access_maybe_read(SceneLocomotionComp);
  ecs_access_maybe_read(SceneStatusComp);
  ecs_access_maybe_read(SceneTargetFinderComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneNameComp);
}

ecs_view_define(MinimapMarkerView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneHealthComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneUnitComp);
}

ecs_view_define(ProductionView) {
  ecs_access_read(SceneNameComp);
  ecs_access_write(SceneProductionComp);
}

ecs_view_define(WeaponMapView) { ecs_access_read(AssetWeaponMapComp); }

static bool hud_rect_intersect(const UiRect a, const UiRect b) {
  return a.x + a.width > b.x && b.x + b.width > a.x && a.y + a.height > b.y && b.y + b.height > a.y;
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

static String hud_faction_name(const SceneFaction faction) {
  switch (faction) {
  case SceneFaction_A:
    return string_lit("Player");
  default:
    return string_lit("Enemy");
  }
}

static UiColor hud_faction_color(const SceneFaction faction) {
  switch (faction) {
  case SceneFaction_A:
    return ui_color(0, 40, 255, 255);
  case SceneFaction_None:
    return ui_color_white;
  default:
    return ui_color(255, 0, 15, 255);
  }
}

static void hud_health_draw(
    UiCanvasComp*    canvas,
    HudComp*         hud,
    const GeoMatrix* viewProj,
    EcsView*         healthView,
    const UiVector   res) {
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
    const UiVector uiPos     = ui_vector(canvasPos.x * res.x, canvasPos.y * res.y);
    const f32      barWidth  = g_hudHealthBarSize.width;
    const f32      barHeight = g_hudHealthBarSize.height;

    const UiRect bounds = {
        .pos  = ui_vector(uiPos.x - barWidth * 0.5f, uiPos.y + g_hudHealthBarOffsetY),
        .size = ui_vector(barWidth, barHeight + g_hudStatusSpacing.y + g_hudStatusIconSize.y),
    };
    if (hud_rect_intersect(hud->minimapRect, bounds)) {
      continue; // Position is over the minimap.
    }

    // Compute the health-bar ui rectangle.
    ui_layout_set_pos(canvas, UiBase_Canvas, uiPos, UiBase_Absolute);
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

  ui_layout_move_to(canvas, UiBase_Container, UiAlign_BottomRight, Ui_XY);
  ui_layout_move(canvas, ui_vector(-g_spacing, g_spacing), UiBase_Absolute, Ui_XY);
  ui_layout_resize(canvas, UiAlign_BottomRight, g_size, UiBase_Absolute, Ui_XY);

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

static void hud_info_draw(UiCanvasComp* canvas, EcsIterator* infoItr, EcsIterator* weaponMapItr) {
  const SceneAttackComp*       attackComp   = ecs_view_read_t(infoItr, SceneAttackComp);
  const SceneDamageStatsComp*  damageStats  = ecs_view_read_t(infoItr, SceneDamageStatsComp);
  const SceneFactionComp*      factionComp  = ecs_view_read_t(infoItr, SceneFactionComp);
  const SceneHealthComp*       healthComp   = ecs_view_read_t(infoItr, SceneHealthComp);
  const SceneLocomotionComp*   locoComp     = ecs_view_read_t(infoItr, SceneLocomotionComp);
  const SceneNameComp*         nameComp     = ecs_view_read_t(infoItr, SceneNameComp);
  const SceneStatusComp*       statusComp   = ecs_view_read_t(infoItr, SceneStatusComp);
  const SceneTargetFinderComp* targetFinder = ecs_view_read_t(infoItr, SceneTargetFinderComp);
  const SceneVisibilityComp*   visComp      = ecs_view_read_t(infoItr, SceneVisibilityComp);

  if (visComp && !scene_visible(visComp, SceneFaction_A)) {
    return; // TODO: Make the local faction configurable instead of hardcoding 'A'.
  }

  const String entityName = stringtable_lookup(g_stringtable, nameComp->name);

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, 4 * usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  fmt_write(&buffer, "\a.bName\ar:\a>15{}\n", fmt_text(entityName));
  if (factionComp) {
    const String  name  = hud_faction_name(factionComp->id);
    const UiColor color = hud_faction_color(factionComp->id);
    fmt_write(&buffer, "\a.bFaction\ar:\a>15{}{}\ar\n", fmt_ui_color(color), fmt_text(name));
  }
  if (healthComp) {
    const u32 healthVal    = (u32)math_round_up_f32(healthComp->max * healthComp->norm);
    const u32 healthMaxVal = (u32)math_round_up_f32(healthComp->max);
    fmt_write(&buffer, "\a.bHealth\ar:\a>15{} / {}\n", fmt_int(healthVal), fmt_int(healthMaxVal));
  }
  if (statusComp && statusComp->active) {
    fmt_write(&buffer, "\a.bStatus\ar:\a>15");
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
  if (targetFinder) {
    fmt_write(
        &buffer,
        "\a.bRange\ar:\a>15{} - {}\n",
        fmt_float(targetFinder->distanceMin, .maxDecDigits = 1),
        fmt_float(targetFinder->distanceMax, .maxDecDigits = 1));
  }
  if (attackComp && weaponMapItr) {
    const AssetWeaponMapComp* weaponMap = ecs_view_read_t(weaponMapItr, AssetWeaponMapComp);
    const AssetWeapon*        weapon    = asset_weapon_get(weaponMap, attackComp->weaponName);
    if (weapon) {
      const f32 damage = asset_weapon_damage(weaponMap, weapon);
      if (damage > f32_epsilon) {
        fmt_write(&buffer, "\a.bDamage\ar:\a>15{}\n", fmt_float(damage, .maxDecDigits = 1));
      }
      if (asset_weapon_apply_burning(weaponMap, weapon)) {
        fmt_write(
            &buffer,
            "\a.bApply\ar:\a>15\a|02{}{}\ar {}\n",
            fmt_ui_color(g_hudStatusIconColors[SceneStatusType_Burning]),
            fmt_text(ui_shape_scratch(g_hudStatusIcons[SceneStatusType_Burning])),
            fmt_text(scene_status_name((SceneStatusType)SceneStatusType_Burning)));
      }
    }
  }
  if (locoComp) {
    fmt_write(&buffer, "\a.bSpeed\ar:\a>15{}\n", fmt_float(locoComp->maxSpeed, .maxDecDigits = 1));
  }
  if (damageStats) {
    fmt_write(&buffer, "\a.bDealt Dmg\ar:\a>15{}\n", fmt_int((u64)damageStats->dealtDamage));
    fmt_write(&buffer, "\a.bKills\ar:\a>15{}\n", fmt_int(damageStats->kills));
  }

  ui_tooltip(canvas, sentinel_u64, dynstring_view(&buffer));
}

static void hud_minimap_update(
    HudComp*                hud,
    const SceneTerrainComp* terrain,
    RendSettingsComp*       rendSettings,
    const UiVector          res) {
  // Compute minimap rect.
  hud->minimapRect = (UiRect){
      .pos  = ui_vector(res.width - g_hudMinimapSize.width, res.height - g_hudMinimapSize.height),
      .size = g_hudMinimapSize,
  };

  // Update renderer minimap settings.
  rendSettings->flags |= RendFlags_Minimap;
  rendSettings->minimapRect[0] = (hud->minimapRect.x - 0.5f) / res.width;
  rendSettings->minimapRect[1] = (hud->minimapRect.y - 0.5f) / res.height;
  rendSettings->minimapRect[2] = (hud->minimapRect.width + 0.5f) / res.width;
  rendSettings->minimapRect[3] = (hud->minimapRect.height + 0.5f) / res.height;
  rendSettings->minimapAlpha   = g_hudMinimapAlpha;
  rendSettings->minimapZoom    = scene_terrain_size(terrain) / g_hudMinimapPlaySize;
}

static UiVector hud_minimap_pos(const GeoVector worldPos, const GeoVector areaSize) {
  const GeoVector pos = geo_vector_add(worldPos, geo_vector_mul(areaSize, 0.5f));
  return ui_vector(pos.x / areaSize.x, pos.z / areaSize.z);
}

static bool hud_minimap_camera_frustum(
    const SceneCameraComp*    cam,
    const SceneTransformComp* camTrans,
    const f32                 camAspect,
    const GeoVector           areaSize,
    UiVector                  out[PARAM_ARRAY_SIZE(4)]) {
  const GeoPlane groundPlane = {.normal = geo_up};

  static const GeoVector g_screenCorners[] = {
      {.x = 0, .y = 0},
      {.x = 0, .y = 1},
      {.x = 1, .y = 1},
      {.x = 1, .y = 0},
  };

  for (u32 i = 0; i != array_elems(g_screenCorners); ++i) {
    const GeoRay ray  = scene_camera_ray(cam, camTrans, camAspect, g_screenCorners[i]);
    const f32    rayT = geo_plane_intersect_ray(&groundPlane, &ray);
    if (rayT < f32_epsilon) {
      return false;
    }
    const GeoVector worldPos = geo_ray_position(&ray, rayT);
    out[i]                   = hud_minimap_pos(worldPos, areaSize);
  }
  return true;
}

typedef struct {
  UiVector pos;
  UiColor  color;
} HudMinimapMarker;

#define hud_minimap_marker_max 2048

static u32 hud_minimap_marker_collect(
    EcsView*         markerView,
    const GeoVector  areaSize,
    HudMinimapMarker out[PARAM_ARRAY_SIZE(hud_minimap_marker_max)]) {
  u32 count = 0;
  for (EcsIterator* itr = ecs_view_itr(markerView); ecs_view_walk(itr);) {
    const SceneFactionComp*    factionComp = ecs_view_read_t(itr, SceneFactionComp);
    const SceneHealthComp*     health      = ecs_view_read_t(itr, SceneHealthComp);
    const SceneTransformComp*  transComp   = ecs_view_read_t(itr, SceneTransformComp);
    const SceneVisibilityComp* visComp     = ecs_view_read_t(itr, SceneVisibilityComp);

    if (visComp && !scene_visible(visComp, SceneFaction_A)) {
      continue; // TODO: Make the local faction configurable instead of hardcoding 'A'.
    }
    if (health->norm < f32_epsilon) {
      continue;
    }

    out[count++] = (HudMinimapMarker){
        .pos   = hud_minimap_pos(transComp->position, areaSize),
        .color = hud_faction_color(factionComp ? factionComp->id : SceneFaction_None),
    };

    if (UNLIKELY(count == hud_minimap_marker_max)) {
      break;
    }
  }
  return count;
}

static void hud_minimap_draw(
    UiCanvasComp*             canvas,
    HudComp*                  hud,
    InputStateComp*           inputState,
    const SceneCameraComp*    cam,
    const SceneTransformComp* camTrans,
    EcsView*                  markerView) {
  const UiVector  canvasRes    = ui_canvas_resolution(canvas);
  const f32       canvasAspect = (f32)canvasRes.width / (f32)canvasRes.height;
  const GeoVector area         = geo_vector(g_hudMinimapPlaySize, 0, g_hudMinimapPlaySize);

  ui_layout_push(canvas);
  ui_layout_set(canvas, hud->minimapRect, UiBase_Absolute);
  ui_style_push(canvas);

  // Draw frame.
  ui_style_color(canvas, ui_color_clear);
  ui_style_outline(canvas, 3);
  const UiId     frameId = ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);
  const UiStatus frameStatus = ui_canvas_elem_status(canvas, frameId);

  // Handle input.
  input_set_allow_zoom_over_ui(inputState, frameStatus >= UiStatus_Hovered);
  if (frameStatus >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
  }
  if (frameStatus >= UiStatus_Pressed) {
    const UiVector uiPos = ui_canvas_input_pos(canvas);
    const f32      x = ((uiPos.x - hud->minimapRect.x) / hud->minimapRect.width - 0.5f) * area.x;
    const f32      z = ((uiPos.y - hud->minimapRect.y) / hud->minimapRect.height - 0.5f) * area.z;
    input_camera_center(inputState, geo_vector(x, 0, z));
  }

  const UiCircleOpts circleOpts = {.base = UiBase_Container, .radius = g_hudMinimapDotRadius};
  const UiLineOpts   lineOpts   = {.base = UiBase_Container, .width = g_hudMinimapLineWidth};

  ui_layout_container_push(canvas, UiClip_Rect);

  // Collect markers.
  HudMinimapMarker markers[hud_minimap_marker_max];
  const u32        markerCount = hud_minimap_marker_collect(markerView, area, markers);

  // Draw marker outlines.
  ui_style_outline(canvas, 2);
  ui_style_color(canvas, ui_color_black);
  for (u32 i = 0; i != markerCount; ++i) {
    const HudMinimapMarker* marker = &markers[i];
    ui_circle_with_opts(canvas, marker->pos, &circleOpts);
  }

  // Draw marker fill.
  ui_style_outline(canvas, 0);
  for (u32 i = 0; i != markerCount; ++i) {
    const HudMinimapMarker* marker = &markers[i];
    ui_style_color(canvas, marker->color);
    ui_circle_with_opts(canvas, marker->pos, &circleOpts);
  }

  // Draw camera frustum.
  ui_style_outline(canvas, 0);
  UiVector camFrustumPoints[4];
  if (hud_minimap_camera_frustum(cam, camTrans, canvasAspect, area, camFrustumPoints)) {
    ui_style_color(canvas, ui_color_white);
    ui_line_with_opts(canvas, camFrustumPoints[0], camFrustumPoints[1], &lineOpts);
    ui_line_with_opts(canvas, camFrustumPoints[1], camFrustumPoints[2], &lineOpts);
    ui_line_with_opts(canvas, camFrustumPoints[2], camFrustumPoints[3], &lineOpts);
    ui_line_with_opts(canvas, camFrustumPoints[3], camFrustumPoints[0], &lineOpts);
  }

  ui_layout_container_pop(canvas);
  ui_canvas_id_block_next(canvas); // End on an consistent id.

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void hud_production_bg_draw(UiCanvasComp* canvas) {
  ui_style_push(canvas);
  ui_style_color(canvas, ui_color(16, 16, 16, 128));
  ui_style_outline(canvas, 3);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);
  ui_style_pop(canvas);
}

static UiId hud_production_header_draw(UiCanvasComp* canvas, EcsIterator* itr) {
  static const f32 g_height = 30;

  const SceneNameComp* nameComp   = ecs_view_read_t(itr, SceneNameComp);
  const String         entityName = stringtable_lookup(g_stringtable, nameComp->name);
  const Unicode        icon       = UiShape_Groups; // TODO: Make the icon configurable.

  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_move_to(canvas, UiBase_Current, UiAlign_TopLeft, Ui_Y);
  ui_layout_resize(canvas, UiAlign_TopLeft, ui_vector(0, g_height), UiBase_Absolute, Ui_Y);

  ui_style_outline(canvas, 3);
  ui_style_color(canvas, ui_color(16, 16, 16, 128));
  const UiId id = ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_outline(canvas, 2);
  ui_style_color(canvas, ui_color_white);
  ui_label(
      canvas,
      fmt_write_scratch("{} {}", fmt_text(ui_shape_scratch(icon)), fmt_text(entityName)),
      .align    = UiAlign_MiddleCenter,
      .fontSize = 22);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  return id;
}

static void hud_production_queue_bg_draw(UiCanvasComp* canvas, const UiStatus status) {
  ui_style_push(canvas);
  switch (status) {
  case UiStatus_Hovered:
    ui_style_color(canvas, ui_color(32, 32, 32, 128));
    ui_style_outline(canvas, 3);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_color(canvas, ui_color(48, 48, 48, 128));
    ui_style_outline(canvas, 1);
    break;
  case UiStatus_Idle:
    ui_style_color(canvas, ui_color(16, 16, 16, 128));
    ui_style_outline(canvas, 2);
    break;
  }
  ui_canvas_draw_glyph(canvas, UiShape_Circle, 15, UiFlags_Interactable);
  ui_style_pop(canvas);
}

static void hud_production_queue_icon_draw(
    UiCanvasComp* canvas, const AssetProduct* product, const UiStatus status) {
  static const UiVector g_size = {.x = 50, .y = 50};

  ui_style_push(canvas);
  ui_layout_push(canvas);

  ui_style_outline(canvas, status == UiStatus_Hovered ? 4 : 2);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleCenter, g_size, UiBase_Absolute);
  ui_canvas_draw_glyph(canvas, product->icon, 0, UiFlags_None);

  ui_layout_pop(canvas);
  ui_style_pop(canvas);
}

static void hud_production_queue_count_draw(UiCanvasComp* canvas, const SceneProductQueue* queue) {
  static const UiVector g_size = {.x = 35, .y = 40};

  ui_style_push(canvas);
  ui_layout_push(canvas);

  ui_style_weight(canvas, UiWeight_Bold);
  ui_style_outline(canvas, 3);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_TopLeft, g_size, UiBase_Absolute);
  const String countText = fmt_write_scratch("{}", fmt_int(queue->count));
  ui_label(canvas, countText, .align = UiAlign_MiddleCenter, .fontSize = 25);

  ui_layout_pop(canvas);
  ui_style_pop(canvas);
}

static void hud_production_queue_cost_draw(UiCanvasComp* canvas, const AssetProduct* product) {
  static const UiVector g_size = {.x = 70, .y = 30};

  ui_layout_push(canvas);

  ui_layout_inner(canvas, UiBase_Current, UiAlign_BottomLeft, g_size, UiBase_Absolute);
  const String costText = fmt_write_scratch("\uE425 {}", fmt_duration(product->costTime));
  ui_label(canvas, costText, .align = UiAlign_MiddleCenter);

  ui_layout_pop(canvas);
}

static void hud_production_queue_draw(
    UiCanvasComp* canvas, SceneProductionComp* production, const u32 queueIndex) {
  const SceneProductQueue* queue   = production->queues + queueIndex;
  const AssetProduct*      product = queue->product;

  const UiId     id     = ui_canvas_id_peek(canvas);
  const UiStatus status = ui_canvas_elem_status(canvas, id);

  hud_production_queue_bg_draw(canvas, status);
  hud_production_queue_icon_draw(canvas, product, status);
  if (queue->count) {
    hud_production_queue_count_draw(canvas, queue);
  }
  hud_production_queue_cost_draw(canvas, product);

  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
  }
  if (status == UiStatus_Activated) {
    ui_canvas_sound(canvas, UiSoundType_Click);
  }
  if (!string_is_empty(product->name)) {
    ui_tooltip(canvas, id, product->name);
  }
}

static void hud_production_draw(UiCanvasComp* canvas, HudComp* hud, EcsIterator* itr) {
  ui_layout_push(canvas);
  ui_layout_set(canvas, ui_rect(ui_vector(0, 0), g_hudProductionSize), UiBase_Absolute);

  hud_production_bg_draw(canvas);
  hud_production_header_draw(canvas, itr);

  SceneProductionComp* production     = ecs_view_write_t(itr, SceneProductionComp);
  const u32            colCount       = 2;
  const u32            rowCount       = production->queueCount / colCount + 1;
  const f32            spacing        = 10.0f;
  const f32            scrollbarWidth = 10.0f;
  const f32            availableWidth = g_hudProductionSize.width - scrollbarWidth;
  const f32            entrySize      = (availableWidth - (colCount + 1) * spacing) / colCount;
  const UiVector       entrySizeVec   = ui_vector(entrySize, entrySize);
  const f32            height         = rowCount * entrySize + (rowCount + 1) * spacing;

  ui_layout_grow(canvas, UiAlign_BottomCenter, ui_vector(0, -33), UiBase_Absolute, Ui_Y);
  ui_scrollview_begin(canvas, &hud->productionScrollView, height);

  ui_layout_move_to(canvas, UiBase_Current, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(canvas, UiAlign_TopLeft, entrySizeVec, UiBase_Absolute, Ui_XY);
  ui_layout_move_dir(canvas, Ui_Down, spacing, UiBase_Absolute);

  for (u32 row = 0; row != rowCount; ++row) {
    ui_layout_move_to(canvas, UiBase_Container, UiAlign_TopLeft, Ui_X);
    ui_layout_move_dir(canvas, Ui_Right, spacing, UiBase_Absolute);

    for (u32 col = 0; col != colCount; ++col) {
      const u32 queueIndex = row * colCount + col;
      if (queueIndex < production->queueCount) {
        hud_production_queue_draw(canvas, production, queueIndex);
      }
      ui_layout_move_dir(canvas, Ui_Right, entrySize + spacing, UiBase_Absolute);
    }
    ui_layout_move_dir(canvas, Ui_Down, entrySize + spacing, UiBase_Absolute);
  }

  ui_scrollview_end(canvas, &hud->productionScrollView);
  ui_layout_pop(canvas);
}

ecs_system_define(HudDrawUiSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  CmdControllerComp*             cmd       = ecs_view_write_t(globalItr, CmdControllerComp);
  const InputManagerComp*        input     = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneSelectionComp*      sel       = ecs_view_read_t(globalItr, SceneSelectionComp);
  const SceneTerrainComp*        terrain   = ecs_view_read_t(globalItr, SceneTerrainComp);
  const SceneWeaponResourceComp* weaponRes = ecs_view_read_t(globalItr, SceneWeaponResourceComp);

  EcsView* hudView           = ecs_world_view_t(world, HudView);
  EcsView* canvasView        = ecs_world_view_t(world, UiCanvasView);
  EcsView* healthView        = ecs_world_view_t(world, HealthView);
  EcsView* infoView          = ecs_world_view_t(world, InfoView);
  EcsView* weaponMapView     = ecs_world_view_t(world, WeaponMapView);
  EcsView* minimapMarkerView = ecs_world_view_t(world, MinimapMarkerView);
  EcsView* productionView    = ecs_world_view_t(world, ProductionView);

  EcsIterator* canvasItr     = ecs_view_itr(canvasView);
  EcsIterator* infoItr       = ecs_view_itr(infoView);
  EcsIterator* productionItr = ecs_view_itr(productionView);
  EcsIterator* weaponMapItr  = ecs_view_maybe_at(weaponMapView, scene_weapon_map(weaponRes));

  for (EcsIterator* itr = ecs_view_itr(hudView); ecs_view_walk(itr);) {
    InputStateComp*           inputState   = ecs_view_write_t(itr, InputStateComp);
    const SceneCameraComp*    cam          = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* camTrans     = ecs_view_read_t(itr, SceneTransformComp);
    RendSettingsComp*         rendSettings = ecs_view_write_t(itr, RendSettingsComp);
    HudComp*                  hud          = ecs_view_write_t(itr, HudComp);
    if (!ecs_view_maybe_jump(canvasItr, hud->uiCanvas)) {
      continue;
    }
    UiCanvasComp*   canvas   = ecs_view_write_t(canvasItr, UiCanvasComp);
    const GeoMatrix viewProj = hud_ui_view_proj(cam, camTrans, canvas);

    ui_canvas_reset(canvas);
    if (input_layer_active(input, string_hash_lit("Debug"))) {
      rendSettings->flags &= ~RendFlags_Minimap;
      continue;
    }
    const UiVector res = ui_canvas_resolution(canvas);
    if (UNLIKELY(res.x < f32_epsilon || res.y < f32_epsilon)) {
      continue;
    }
    ui_canvas_to_back(canvas);

    hud_minimap_update(hud, terrain, rendSettings, res);

    hud_health_draw(canvas, hud, &viewProj, healthView, res);
    hud_groups_draw(canvas, cmd);
    hud_minimap_draw(canvas, hud, inputState, cam, camTrans, minimapMarkerView);

    if (ecs_view_maybe_jump(productionItr, scene_selection_main(sel))) {
      hud_production_draw(canvas, hud, productionItr);
    }

    const EcsEntityId  hoveredEntity = input_hovered_entity(inputState);
    const TimeDuration hoveredTime   = input_hovered_time(inputState);
    if (hoveredTime >= time_second && ecs_view_maybe_jump(infoItr, hoveredEntity)) {
      hud_info_draw(canvas, infoItr, weaponMapItr);
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
  ecs_register_view(WeaponMapView);
  ecs_register_view(MinimapMarkerView);
  ecs_register_view(ProductionView);

  ecs_register_system(
      HudDrawUiSys,
      ecs_view_id(GlobalView),
      ecs_view_id(HudView),
      ecs_view_id(UiCanvasView),
      ecs_view_id(HealthView),
      ecs_view_id(InfoView),
      ecs_view_id(WeaponMapView),
      ecs_view_id(MinimapMarkerView),
      ecs_view_id(ProductionView));

  enum {
    Order_Normal    = 0,
    Order_HudDrawUi = 1,
  };
  ecs_order(HudDrawUiSys, Order_HudDrawUi);
}

void hud_init(EcsWorld* world, const EcsEntityId cameraEntity) {
  diag_assert_msg(!ecs_world_has_t(world, cameraEntity, HudComp), "HUD already active");
  ecs_world_add_t(
      world,
      cameraEntity,
      HudComp,
      .uiCanvas = ui_canvas_create(world, cameraEntity, UiCanvasCreateFlags_None));
}
