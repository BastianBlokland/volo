#include "asset_manager.h"
#include "asset_product.h"
#include "asset_weapon.h"
#include "core_alloc.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "gap_input.h"
#include "input_manager.h"
#include "rend_object.h"
#include "scene_attack.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_level.h"
#include "scene_lifetime.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_product.h"
#include "scene_set.h"
#include "scene_status.h"
#include "scene_target.h"
#include "scene_terrain.h"
#include "scene_transform.h"
#include "scene_visibility.h"
#include "scene_weapon.h"
#include "trace_tracer.h"
#include "ui.h"

#include "cmd_internal.h"
#include "hud_internal.h"
#include "input_internal.h"

static const f32      g_hudHealthBarOffsetY = 10.0f;
static const UiVector g_hudHealthBarSize    = {.x = 50.0f, .y = 7.5f};

static const Unicode g_hudStatusIcons[SceneStatusType_Count] = {
    [SceneStatusType_Burning]  = UiShape_Whatshot,
    [SceneStatusType_Bleeding] = UiShape_Droplet,
    [SceneStatusType_Healing]  = UiShape_Hospital,
    [SceneStatusType_Veteran]  = UiShape_Star,
};
static const UiColor g_hudStatusIconColors[SceneStatusType_Count] = {
    [SceneStatusType_Burning]  = {.r = 255, .g = 128, .b = 0, .a = 255},
    [SceneStatusType_Bleeding] = {.r = 255, .g = 0, .b = 0, .a = 255},
    [SceneStatusType_Healing]  = {.r = 0, .g = 255, .b = 0, .a = 255},
    [SceneStatusType_Veteran]  = {.r = 255, .g = 175, .b = 55, .a = 255},
};
static const UiVector g_hudStatusIconSize   = {.x = 15.0f, .y = 15.0f};
static const UiVector g_hudStatusSpacing    = {.x = 2.0f, .y = 4.0f};
static const UiVector g_hudMinimapSize      = {.x = 300.0f, .y = 300.0f};
static const f32      g_hudMinimapAlpha     = 0.95f;
static const f32      g_hudMinimapDotRadius = 2.0f;
static const f32      g_hudMinimapLineWidth = 2.5f;
static const UiVector g_hudProductionSize   = {.x = 300.0f, .y = 400.0f};
static StringHash     g_hudProductQueueActions[3];

ecs_comp_define(HudComp) {
  EcsEntityId  uiCanvas;
  UiRect       minimapRect;
  UiScrollview productionScrollView;

  EcsEntityId rendObjMinimap, rendObjIndicatorRing, rendObjIndicatorBox;
};

ecs_view_define(GlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneLevelManagerComp);
  ecs_access_read(SceneSetEnvComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneWeaponResourceComp);
  ecs_access_write(CmdControllerComp);
}

ecs_view_define(HudView) {
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(HudComp);
  ecs_access_write(InputStateComp);
}

ecs_view_define(UiCanvasView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the canvas's we create.
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(RendObjView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the render objects we create.
  ecs_access_write(RendObjectComp);
}

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
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneHealthComp);
  ecs_access_maybe_read(SceneHealthStatsComp);
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
  ecs_access_read(SceneSetMemberComp);
}

ecs_view_define(ProductionView) {
  ecs_access_read(SceneNameComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneProductionComp);
}

ecs_view_define(VisionView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneVisionComp);
}

ecs_view_define(WeaponMapView) { ecs_access_read(AssetWeaponMapComp); }

static EcsEntityId hud_rend_obj_create(
    EcsWorld*         world,
    AssetManagerComp* assets,
    const EcsEntityId window,
    const String      graphic,
    const bool        post /* To be drawn in the post pass */) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneLifetimeOwnerComp, .owners[0] = window);

  RendObjectFlags flags = RendObjectFlags_Preload;
  if (post) {
    flags |= RendObjectFlags_Post;
  }

  RendObjectComp* obj = rend_draw_create(world, e, flags);
  rend_draw_set_resource(obj, RendObjectResource_Graphic, asset_lookup(world, assets, graphic));
  rend_draw_set_camera_filter(obj, window);
  return e;
}

static void hud_indicator_ring_draw(
    const HudComp*  hud,
    EcsIterator*    rendObjItr,
    const GeoVector center,
    const f32       radius,
    const UiColor   color) {
  ecs_view_jump(rendObjItr, hud->rendObjIndicatorRing);
  RendObjectComp* obj = ecs_view_write_t(rendObjItr, RendObjectComp);

  typedef struct {
    ALIGNAS(16)
    f32     center[3];
    f32     radius;
    u32     vertexCount;
    UiColor color;
    u32     padding[2];
  } RingData;

  // NOTE: Vertex count can unfortunately not be dynamic as the renderer only supports specifying a
  // custom vertex count per draw, and not per instance.
  const u32 vertexCount = 200;
  rend_draw_set_vertex_count(obj, vertexCount);

  const f32    maxThickness = 0.5f; // Should be bigger or equal to the thickness in the shader.
  const GeoBox bounds       = geo_box_from_center(
      center, geo_vector((radius + maxThickness) * 2.0f, 1.0f, (radius + maxThickness) * 2.0f));

  *rend_draw_add_instance_t(obj, RingData, SceneTags_Vfx, bounds) = (RingData){
      .center[0]   = center.x,
      .center[1]   = center.y,
      .center[2]   = center.z,
      .radius      = radius,
      .vertexCount = vertexCount,
      .color       = color,
  };
}

static void hud_indicator_box_draw(
    const HudComp* hud, EcsIterator* rendObjItr, const GeoBox* box, const UiColor color) {
  ecs_view_jump(rendObjItr, hud->rendObjIndicatorBox);
  RendObjectComp* obj = ecs_view_write_t(rendObjItr, RendObjectComp);

  typedef struct {
    ALIGNAS(16)
    f32     center[3];
    f32     padding1;
    f32     width;
    f32     height;
    UiColor color;
    f32     padding2;
  } BoxData;

  const f32       maxThickness = 0.5f; // Should be bigger or equal to the thickness in the shader.
  const GeoVector center       = geo_box_center(box);
  const GeoVector size         = geo_box_size(box);
  const GeoBox    bounds       = geo_box_dilate(box, geo_vector(maxThickness, 1.0f, maxThickness));

  *rend_draw_add_instance_t(obj, BoxData, SceneTags_Vfx, bounds) = (BoxData){
      .center[0] = center.x,
      .center[1] = center.y,
      .center[2] = center.z,
      .width     = size.x,
      .height    = size.z,
      .color     = color,
  };
}

INLINE_HINT static bool hud_rect_intersect(const UiRect a, const UiRect b) {
  return a.x + a.width > b.x && b.x + b.width > a.x && a.y + a.height > b.y && b.y + b.height > a.y;
}

static GeoMatrix hud_ui_view_proj(
    const SceneCameraComp* cam, const SceneTransformComp* camTrans, const UiCanvasComp* c) {
  const UiVector res    = ui_canvas_resolution(c);
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

static void hud_level_draw(UiCanvasComp* c, const SceneLevelManagerComp* level) {
  const String name = scene_level_name(level);
  if (!string_is_empty(name)) {
    ui_layout_push(c);
    ui_layout_inner(c, UiBase_Canvas, UiAlign_TopCenter, ui_vector(500, 100), UiBase_Absolute);

    ui_style_push(c);
    ui_style_color(c, ui_color_white);
    ui_style_outline(c, 5);

    ui_label(c, name, .align = UiAlign_MiddleCenter, .fontSize = 40);

    ui_style_pop(c);
    ui_layout_pop(c);
  }
}

static void hud_health_draw(
    UiCanvasComp*    c,
    HudComp*         hud,
    const GeoMatrix* viewProj,
    EcsView*         healthView,
    const UiVector   res) {
  ui_style_push(c);
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
    if (!hud_rect_intersect(bounds, ui_rect(ui_vector(0, 0), res))) {
      continue; // Position is outside of the screen.
    }
    if (hud_rect_intersect(hud->minimapRect, bounds)) {
      continue; // Position is over the minimap.
    }

    // Compute the health-bar ui rectangle.
    ui_layout_set_pos(c, UiBase_Canvas, uiPos, UiBase_Absolute);
    ui_layout_move_dir(c, Ui_Up, g_hudHealthBarOffsetY, UiBase_Absolute);
    ui_layout_resize(c, UiAlign_MiddleCenter, g_hudHealthBarSize, UiBase_Absolute, Ui_XY);

    // Draw the health-bar background.
    ui_style_outline(c, 1);
    ui_style_color(c, ui_color(8, 8, 8, 192));
    ui_canvas_draw_glyph(c, UiShape_Circle, 4, UiFlags_None);

    // Draw the health-bar foreground.
    ui_style_color(c, hud_health_color(health->norm));
    ui_layout_resize(c, UiAlign_MiddleLeft, ui_vector(health->norm, 0), UiBase_Current, Ui_X);
    ui_canvas_draw_glyph(c, UiShape_Circle, 4, UiFlags_None);

    if (status && status->active) {
      ui_layout_next(c, Ui_Up, g_hudStatusSpacing.y);
      ui_layout_resize(c, UiAlign_BottomLeft, g_hudStatusIconSize, UiBase_Absolute, Ui_XY);
      bitset_for(bitset_from_var(status->active), typeIndex) {
        ui_style_outline(c, 2);
        ui_style_color(c, g_hudStatusIconColors[typeIndex]);
        ui_canvas_draw_glyph(c, g_hudStatusIcons[typeIndex], 0, UiFlags_None);
        ui_layout_next(c, Ui_Right, g_hudStatusSpacing.x);
      }
    }
  }
  ui_style_pop(c);
  ui_canvas_id_block_next(c); // End on an consistent id.
}

static void hud_groups_draw(UiCanvasComp* c, CmdControllerComp* cmd) {
  static const UiVector g_size    = {50, 25};
  static const f32      g_spacing = 8.0f;

  ui_layout_move_to(c, UiBase_Container, UiAlign_BottomRight, Ui_XY);
  ui_layout_move(c, ui_vector(-g_spacing, g_spacing), UiBase_Absolute, Ui_XY);
  ui_layout_resize(c, UiAlign_BottomRight, g_size, UiBase_Absolute, Ui_XY);

  for (u32 i = cmd_group_count; i-- != 0;) {
    const u32 size = cmd_group_size(cmd, i);
    if (!size) {
      continue;
    }
    if (ui_button(
            c,
            .label      = fmt_write_scratch("\a|02{}\ar {}", fmt_int(i + 1), fmt_ui_shape(Group)),
            .fontSize   = 20,
            .frameColor = ui_color(32, 32, 32, 192),
            .tooltip    = fmt_write_scratch("Size: {}", fmt_int(size)))) {
      cmd_push_select_group(cmd, i);
    }
    ui_layout_next(c, Ui_Up, g_spacing);
  }
}

static void hud_info_stat_write(const f32 org, const f32 modified, DynString* out) {
  format_write_float(out, modified, .maxDecDigits = 1);

  const f32 modDiff    = modified - org;
  const f32 modDiffAbs = math_abs(modDiff);
  if (modDiffAbs > f32_epsilon) {
    fmt_write(
        out,
        " ({}{}{}\ar)",
        fmt_ui_color(modDiff < 0.0f ? ui_color_red : ui_color_green),
        fmt_char(modDiff < 0.0f ? '-' : '+'),
        fmt_float(modDiffAbs, .maxDecDigits = 1));
  }
}

static void hud_info_status_mask_write(const SceneStatusMask statusMask, DynString* out) {
  bool first = true;
  bitset_for(bitset_from_var(statusMask), typeIndex) {
    if (!first) {
      dynstring_append(out, string_lit(", "));
    }
    first = false;
    fmt_write(
        out,
        "\a|02{}{}\ar {}",
        fmt_ui_color(g_hudStatusIconColors[typeIndex]),
        fmt_text(ui_shape_scratch(g_hudStatusIcons[typeIndex])),
        fmt_text(scene_status_name((SceneStatusType)typeIndex)));
  }
}

static void hud_info_health_stats_write(const SceneHealthStatsComp* stats, DynString* out) {
  static const String g_healthStatNames[SceneHealthStat_Count] = {
      [SceneHealthStat_DealtDamage]  = string_static("Dealt Dmg"),
      [SceneHealthStat_DealtHealing] = string_static("Dealt Heal"),
      [SceneHealthStat_Kills]        = string_static("Kills"),
  };
  for (SceneHealthStat stat = 0; stat != SceneHealthStat_Count; ++stat) {
    const f32 value        = stats->values[stat];
    const u64 valueRounded = (u64)math_round_nearest_f32(value);
    if (string_is_empty(g_healthStatNames[stat]) || !valueRounded) {
      continue;
    }
    fmt_write(out, "\a.b{}\ar:\a>15{}\n", fmt_text(g_healthStatNames[stat]), fmt_int(valueRounded));
  }
}

static void hud_info_draw(UiCanvasComp* c, EcsIterator* infoItr, EcsIterator* weaponMapItr) {
  const SceneAttackComp*       attackComp       = ecs_view_read_t(infoItr, SceneAttackComp);
  const SceneFactionComp*      factionComp      = ecs_view_read_t(infoItr, SceneFactionComp);
  const SceneHealthComp*       healthComp       = ecs_view_read_t(infoItr, SceneHealthComp);
  const SceneHealthStatsComp*  healthStatsComp  = ecs_view_read_t(infoItr, SceneHealthStatsComp);
  const SceneLocomotionComp*   locoComp         = ecs_view_read_t(infoItr, SceneLocomotionComp);
  const SceneNameComp*         nameComp         = ecs_view_read_t(infoItr, SceneNameComp);
  const SceneStatusComp*       statusComp       = ecs_view_read_t(infoItr, SceneStatusComp);
  const SceneTargetFinderComp* targetFinderComp = ecs_view_read_t(infoItr, SceneTargetFinderComp);
  const SceneVisibilityComp*   visComp          = ecs_view_read_t(infoItr, SceneVisibilityComp);

  if (visComp && !scene_visible(visComp, SceneFaction_A)) {
    return; // TODO: Make the local faction configurable instead of hardcoding 'A'.
  }

  const String entityName = stringtable_lookup(g_stringtable, nameComp->name);

  Mem       bufferMem = alloc_alloc(g_allocScratch, 4 * usize_kibibyte, 1);
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
    hud_info_status_mask_write(statusComp->active, &buffer);
    dynstring_append_char(&buffer, '\n');
  }
  if (targetFinderComp) {
    fmt_write(
        &buffer,
        "\a.bRange\ar:\a>15{} - {}\n",
        fmt_float(targetFinderComp->rangeMin, .maxDecDigits = 1),
        fmt_float(targetFinderComp->rangeMax, .maxDecDigits = 1));
  }
  if (attackComp && weaponMapItr) {
    const AssetWeaponMapComp* weaponMap = ecs_view_read_t(weaponMapItr, AssetWeaponMapComp);
    const AssetWeapon*        weapon    = asset_weapon_get(weaponMap, attackComp->weaponName);
    if (weapon) {
      const f32 damageMult = statusComp ? scene_status_damage(statusComp) : 1.0f;
      const f32 damageOrg  = asset_weapon_damage(weaponMap, weapon);
      const f32 damageMod  = damageOrg * damageMult;
      if (damageOrg > f32_epsilon) {
        fmt_write(&buffer, "\a.bDamage\ar:\a>15");
        hud_info_stat_write(damageOrg, damageMod, &buffer);
        dynstring_append_char(&buffer, '\n');
      }
      const SceneStatusMask appliesStatus = asset_weapon_applies_status(weaponMap, weapon);
      if (appliesStatus) {
        fmt_write(&buffer, "\a.bApply\ar:\a>15");
        hud_info_status_mask_write(appliesStatus, &buffer);
        dynstring_append_char(&buffer, '\n');
      }
    }
  }
  if (locoComp) {
    const f32 speedMult = scene_status_move_speed(statusComp);
    const f32 speedOrg  = locoComp->maxSpeed;
    const f32 speedMod  = speedOrg * speedMult;
    fmt_write(&buffer, "\a.bSpeed\ar:\a>15");
    hud_info_stat_write(speedOrg, speedMod, &buffer);
    dynstring_append_char(&buffer, '\n');
  }
  if (healthStatsComp) {
    hud_info_health_stats_write(healthStatsComp, &buffer);
  }

  ui_tooltip(c, sentinel_u64, dynstring_view(&buffer));
}

static void hud_minimap_update(
    HudComp* hud, EcsIterator* rendObjItr, const SceneTerrainComp* terrain, const UiVector res) {
  // Compute minimap rect.
  hud->minimapRect = (UiRect){
      .pos  = ui_vector(res.width - g_hudMinimapSize.width, res.height - g_hudMinimapSize.height),
      .size = g_hudMinimapSize,
  };

  // Update the minimap background object.
  if (!scene_terrain_loaded(terrain)) {
    return; // Terrain data is required to update the minimap.
  }
  ecs_view_jump(rendObjItr, hud->rendObjMinimap);
  RendObjectComp* obj = ecs_view_write_t(rendObjItr, RendObjectComp);

  typedef struct {
    ALIGNAS(16)
    f32      rect[4]; // x, y, width, height.
    f32      alpha;
    f32      terrainFrac;
    f32      unused[2];
    GeoColor colorLow, colorHigh;
  } MinimapData;

  const EcsEntityId heightmap = scene_terrain_resource_heightmap(terrain);
  diag_assert(heightmap);

  rend_draw_set_resource(obj, RendObjectResource_Texture, heightmap);

  *rend_draw_add_instance_t(obj, MinimapData, SceneTags_None, geo_box_inverted3()) = (MinimapData){
      .rect[0]     = (hud->minimapRect.x - 0.5f) / res.width,
      .rect[1]     = (hud->minimapRect.y - 0.5f) / res.height,
      .rect[2]     = (hud->minimapRect.width + 0.5f) / res.width,
      .rect[3]     = (hud->minimapRect.height + 0.5f) / res.height,
      .alpha       = g_hudMinimapAlpha,
      .terrainFrac = scene_terrain_play_size(terrain) / scene_terrain_size(terrain),
      .colorLow    = scene_terrain_minimap_color_low(terrain),
      .colorHigh   = scene_terrain_minimap_color_high(terrain),
  };
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
  static const GeoPlane  g_groundPlane     = {.normal = {.y = 1.0f}};
  static const GeoVector g_screenCorners[] = {
      {.x = 0, .y = 0},
      {.x = 0, .y = 1},
      {.x = 1, .y = 1},
      {.x = 1, .y = 0},
  };

  for (u32 i = 0; i != array_elems(g_screenCorners); ++i) {
    const GeoRay ray  = scene_camera_ray(cam, camTrans, camAspect, g_screenCorners[i]);
    const f32    rayT = geo_plane_intersect_ray(&g_groundPlane, &ray);
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
  const StringHash minimapSet = string_hash_lit("minimap");

  u32 count = 0;
  for (EcsIterator* itr = ecs_view_itr(markerView); ecs_view_walk(itr);) {
    const SceneFactionComp*    factionComp = ecs_view_read_t(itr, SceneFactionComp);
    const SceneHealthComp*     health      = ecs_view_read_t(itr, SceneHealthComp);
    const SceneTransformComp*  transComp   = ecs_view_read_t(itr, SceneTransformComp);
    const SceneVisibilityComp* visComp     = ecs_view_read_t(itr, SceneVisibilityComp);
    const SceneSetMemberComp*  setMember   = ecs_view_read_t(itr, SceneSetMemberComp);

    if (visComp && !scene_visible(visComp, SceneFaction_A)) {
      continue; // TODO: Make the local faction configurable instead of hardcoding 'A'.
    }
    if (health->norm < f32_epsilon) {
      continue;
    }
    if (!scene_set_member_contains(setMember, minimapSet)) {
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
    UiCanvasComp*             c,
    HudComp*                  hud,
    InputStateComp*           inputState,
    const SceneTerrainComp*   terrain,
    const SceneCameraComp*    cam,
    const SceneTransformComp* camTrans,
    EcsView*                  markerView) {
  const UiVector canvasRes    = ui_canvas_resolution(c);
  const f32      canvasAspect = (f32)canvasRes.width / (f32)canvasRes.height;

  if (!scene_terrain_loaded(terrain)) {
    return;
  }
  const f32       areaSizeAxis = scene_terrain_play_size(terrain);
  const GeoVector areaSize     = geo_vector(areaSizeAxis, 0, areaSizeAxis);

  ui_layout_push(c);
  ui_layout_set(c, hud->minimapRect, UiBase_Absolute);
  ui_style_push(c);

  // Draw frame.
  ui_style_color(c, ui_color_clear);
  ui_style_outline(c, 3);
  const UiId     frameId     = ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_Interactable);
  const UiStatus frameStatus = ui_canvas_elem_status(c, frameId);

  // Handle input.
  input_set_allow_zoom_over_ui(inputState, frameStatus >= UiStatus_Hovered);
  if (frameStatus >= UiStatus_Hovered) {
    ui_canvas_interact_type(c, UiInteractType_Action);
  }
  if (frameStatus >= UiStatus_Pressed) {
    const UiVector uiPos = ui_canvas_input_pos(c);
    const f32 x = ((uiPos.x - hud->minimapRect.x) / hud->minimapRect.width - 0.5f) * areaSize.x;
    const f32 z = ((uiPos.y - hud->minimapRect.y) / hud->minimapRect.height - 0.5f) * areaSize.z;
    input_camera_center(inputState, geo_vector(x, 0, z));
  }

  const UiCircleOpts circleOpts = {.base = UiBase_Container, .radius = g_hudMinimapDotRadius};
  const UiLineOpts   lineOpts   = {.base = UiBase_Container, .width = g_hudMinimapLineWidth};

  ui_layout_container_push(c, UiClip_Rect);

  // Collect markers.
  HudMinimapMarker markers[hud_minimap_marker_max];
  const u32        markerCount = hud_minimap_marker_collect(markerView, areaSize, markers);

  // Draw marker outlines.
  ui_style_outline(c, 2);
  ui_style_color(c, ui_color_black);
  for (u32 i = 0; i != markerCount; ++i) {
    const HudMinimapMarker* marker = &markers[i];
    ui_circle_with_opts(c, marker->pos, &circleOpts);
  }

  // Draw marker fill.
  ui_style_outline(c, 0);
  for (u32 i = 0; i != markerCount; ++i) {
    const HudMinimapMarker* marker = &markers[i];
    ui_style_color(c, marker->color);
    ui_circle_with_opts(c, marker->pos, &circleOpts);
  }

  // Draw camera frustum.
  ui_style_outline(c, 0);
  UiVector camFrustumPoints[4];
  if (hud_minimap_camera_frustum(cam, camTrans, canvasAspect, areaSize, camFrustumPoints)) {
    ui_style_color(c, ui_color_white);
    ui_line_with_opts(c, camFrustumPoints[0], camFrustumPoints[1], &lineOpts);
    ui_line_with_opts(c, camFrustumPoints[1], camFrustumPoints[2], &lineOpts);
    ui_line_with_opts(c, camFrustumPoints[2], camFrustumPoints[3], &lineOpts);
    ui_line_with_opts(c, camFrustumPoints[3], camFrustumPoints[0], &lineOpts);
  }

  ui_layout_container_pop(c);
  ui_canvas_id_block_next(c); // End on an consistent id.

  ui_style_pop(c);
  ui_layout_pop(c);
}

static void hud_vision_draw(HudComp* hud, EcsIterator* rendObjItr, EcsIterator* itr) {
  const SceneVisionComp* vision = ecs_view_read_t(itr, SceneVisionComp);
  if (vision->flags & SceneVisionFlags_ShowInHud) {
    const GeoVector pos = ecs_view_read_t(itr, SceneTransformComp)->position;
    hud_indicator_ring_draw(hud, rendObjItr, pos, vision->radius, ui_color_white);
  }
}

static void hud_production_bg_draw(UiCanvasComp* c) {
  ui_style_push(c);
  ui_style_color(c, ui_color(16, 16, 16, 128));
  ui_style_outline(c, 3);
  ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_Interactable);
  ui_style_pop(c);
}

static UiId hud_production_header_draw(UiCanvasComp* c, EcsIterator* itr) {
  static const f32 g_height = 30;

  const SceneNameComp* nameComp   = ecs_view_read_t(itr, SceneNameComp);
  const String         entityName = stringtable_lookup(g_stringtable, nameComp->name);

  ui_layout_push(c);
  ui_style_push(c);

  ui_layout_move_to(c, UiBase_Current, UiAlign_TopLeft, Ui_Y);
  ui_layout_resize(c, UiAlign_TopLeft, ui_vector(0, g_height), UiBase_Absolute, Ui_Y);

  ui_style_outline(c, 3);
  ui_style_color(c, ui_color(16, 16, 16, 128));
  const UiId id = ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_outline(c, 2);
  ui_style_color(c, ui_color_white);
  ui_label(c, entityName, .align = UiAlign_MiddleCenter, .fontSize = 22);

  ui_style_pop(c);
  ui_layout_pop(c);
  return id;
}

static void hud_production_queue_bg_draw(
    UiCanvasComp* c, const SceneProductQueue* queue, const UiStatus status) {
  ui_style_push(c);
  switch (status) {
  case UiStatus_Hovered:
    ui_style_color(c, ui_color(255, 255, 255, 255));
    ui_style_outline(c, 3);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
  case UiStatus_ActivatedAlt:
    ui_style_color(c, ui_color(225, 225, 225, 255));
    ui_style_outline(c, 1);
    break;
  case UiStatus_Idle:
    ui_style_color(c, ui_color(178, 178, 178, 255));
    ui_style_outline(c, 2);
    break;
  }
  const UiFlags flags = UiFlags_Interactable | UiFlags_InteractSupportAlt;
  ui_canvas_draw_image(c, queue->product->iconImage, 0, flags);
  ui_style_pop(c);
}

static void hud_production_queue_progress_draw(UiCanvasComp* c, const f32 progress) {
  ui_layout_push(c);
  ui_style_push(c);

  ui_style_color(c, ui_color(0, 78, 0, 128));
  ui_style_outline(c, 0);
  ui_layout_resize(c, UiAlign_BottomLeft, ui_vector(progress, 0), UiBase_Current, Ui_X);
  ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_None);

  ui_style_pop(c);
  ui_layout_pop(c);
}

static void hud_production_queue_count_draw(UiCanvasComp* c, const SceneProductQueue* queue) {
  static const UiVector g_size = {.x = 30, .y = 30};

  ui_style_push(c);
  ui_layout_push(c);

  ui_style_weight(c, UiWeight_Bold);
  ui_style_outline(c, 2);
  ui_layout_inner(c, UiBase_Current, UiAlign_TopLeft, g_size, UiBase_Absolute);
  const String countText = fmt_write_scratch("{}", fmt_int(queue->count));
  ui_label(c, countText, .align = UiAlign_MiddleCenter, .fontSize = 20);

  ui_layout_pop(c);
  ui_style_pop(c);
}

static void hud_production_queue_hotkey_draw(
    UiCanvasComp* c, const InputManagerComp* input, const StringHash actionHash) {
  static const UiVector g_size = {.x = 20, .y = 20};

  const GapKey  actionPrimaryKey     = input_primary_key(input, actionHash);
  const Unicode actionPrimaryKeyChar = gap_key_char(actionPrimaryKey);
  if (!actionPrimaryKeyChar) {
    return;
  }
  const String hotkeyText = fmt_write_scratch("{}", fmt_char(actionPrimaryKeyChar));

  ui_style_push(c);
  ui_layout_push(c);
  ui_layout_inner(c, UiBase_Current, UiAlign_TopRight, g_size, UiBase_Absolute);
  ui_layout_move(c, ui_vector(-5.0f, -5.0f), UiBase_Absolute, Ui_XY);

  ui_style_weight(c, UiWeight_Bold);
  ui_style_outline(c, 2);

  ui_style_color(c, ui_color(128, 128, 128, 16));
  ui_canvas_draw_glyph(c, UiShape_Circle, 0, UiFlags_None);

  ui_style_color(c, ui_color_white);
  ui_label(c, hotkeyText, .align = UiAlign_MiddleCenter, .fontSize = 14);

  ui_layout_pop(c);
  ui_style_pop(c);
}

static void hud_production_queue_cost_draw(UiCanvasComp* c, const AssetProduct* product) {
  static const UiVector g_size = {.x = 50, .y = 25};

  ui_layout_push(c);

  ui_layout_inner(c, UiBase_Current, UiAlign_BottomLeft, g_size, UiBase_Absolute);
  const String text = fmt_write_scratch("\uE425 {}", fmt_duration(product->costTime));
  ui_label(c, text, .align = UiAlign_MiddleCenter);

  ui_layout_pop(c);
}

static void hud_production_meta_draw(UiCanvasComp* c, const AssetProduct* product) {
  static const UiVector g_size = {.x = 30, .y = 25};

  ui_layout_push(c);

  ui_layout_inner(c, UiBase_Current, UiAlign_BottomRight, g_size, UiBase_Absolute);
  String text = string_empty;
  if (product->type == AssetProduct_Unit) {
    text = fmt_write_scratch("x{}", fmt_int(product->data_unit.unitCount));
  }
  ui_label(c, text, .align = UiAlign_MiddleCenter);

  ui_layout_pop(c);
}

static void hud_production_queue_tooltip(UiCanvasComp* c, const AssetProduct* prod, const UiId id) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, 4 * usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  if (!string_is_empty(prod->name)) {
    fmt_write(&buffer, "\a.bName\ar:\a>10{}\n", fmt_text(prod->name));
  }
  fmt_write(&buffer, "\a.bTime\ar:\a>10{}\n", fmt_duration(prod->costTime));
  if (prod->type == AssetProduct_Unit) {
    fmt_write(&buffer, "\a.bCount\ar:\a>10{}\n", fmt_int(prod->data_unit.unitCount));
  }
  ui_tooltip(c, id, dynstring_view(&buffer));
}

static void hud_production_queue_draw(
    UiCanvasComp*           c,
    const InputManagerComp* input,
    SceneProductionComp*    production,
    const u32               queueIndex) {
  SceneProductQueue*  queue   = production->queues + queueIndex;
  const AssetProduct* product = queue->product;

  const UiId       id     = ui_canvas_id_peek(c);
  const UiStatus   status = ui_canvas_elem_status(c, id);
  const StringHash hotkey =
      queueIndex < array_elems(g_hudProductQueueActions) ? g_hudProductQueueActions[queueIndex] : 0;

  hud_production_queue_bg_draw(c, queue, status);
  if (queue->state >= SceneProductState_Building) {
    const f32 progress = queue->state == SceneProductState_Building ? queue->progress : 1.0f;
    hud_production_queue_progress_draw(c, progress);
  }
  if (queue->count) {
    hud_production_queue_count_draw(c, queue);
  }
  if (hotkey) {
    hud_production_queue_hotkey_draw(c, input, hotkey);
  }
  if (queue->state == SceneProductState_Ready) {
    ui_style_push(c);
    ui_style_weight(c, UiWeight_Heavy);
    ui_label(c, string_lit("READY"), .align = UiAlign_MiddleCenter, .fontSize = 20);
    ui_style_pop(c);
  }
  hud_production_queue_cost_draw(c, product);
  hud_production_meta_draw(c, product);
  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(c, UiInteractType_Action);
  }
  if (status == UiStatus_Activated || input_triggered_hash(input, hotkey)) {
    if (queue->state == SceneProductState_Ready) {
      queue->requests |= SceneProductRequest_Activate;
    } else {
      if (input_modifiers(input) & InputModifier_Control) {
        ui_canvas_sound(c, UiSoundType_ClickAlt);
        queue->requests |= input_modifiers(input) & InputModifier_Shift
                               ? SceneProductRequest_CancelAll
                               : SceneProductRequest_CancelSingle;
      } else {
        ui_canvas_sound(c, UiSoundType_Click);
        queue->requests |= input_modifiers(input) & InputModifier_Shift
                               ? SceneProductRequest_EnqueueBulk
                               : SceneProductRequest_EnqueueSingle;
      }
    }
  }
  if (status == UiStatus_ActivatedAlt) {
    ui_canvas_sound(c, UiSoundType_ClickAlt);
    queue->requests |= input_modifiers(input) & InputModifier_Shift
                           ? SceneProductRequest_CancelAll
                           : SceneProductRequest_CancelSingle;
  }
  hud_production_queue_tooltip(c, product, id);

  ui_canvas_id_block_next(c); // End on an consistent id.
}

static void hud_production_draw(
    UiCanvasComp*           c,
    HudComp*                hud,
    const InputManagerComp* input,
    EcsIterator*            rendObjItr,
    EcsIterator*            itr) {
  ui_layout_push(c);
  ui_layout_set(c, ui_rect(ui_vector(0, 0), g_hudProductionSize), UiBase_Absolute);

  hud_production_bg_draw(c);
  hud_production_header_draw(c, itr);

  SceneProductionComp* production     = ecs_view_write_t(itr, SceneProductionComp);
  const u32            colCount       = 3;
  const u32            rowCount       = production->queueCount / colCount + 1;
  const f32            spacing        = 10.0f;
  const f32            scrollbarWidth = 10.0f;
  const f32            availableWidth = g_hudProductionSize.width - scrollbarWidth;
  const f32            entrySize      = (availableWidth - (colCount + 1) * spacing) / colCount;
  const UiVector       entrySizeVec   = ui_vector(entrySize, entrySize);
  const f32            height         = rowCount * entrySize + (rowCount + 1) * spacing;

  if (production->placementRadius > f32_epsilon) {
    const GeoVector pos = ecs_view_read_t(itr, SceneTransformComp)->position;
    hud_indicator_ring_draw(hud, rendObjItr, pos, production->placementRadius, ui_color_white);
  }
  if (!(production->flags & SceneProductFlags_RallyLocalSpace)) {
    hud_indicator_ring_draw(hud, rendObjItr, production->rallyPos, 0.25f, ui_color(0, 128, 0, 255));
  }

  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -33), UiBase_Absolute, Ui_Y);
  ui_scrollview_begin(c, &hud->productionScrollView, height);

  ui_layout_move_to(c, UiBase_Current, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(c, UiAlign_TopLeft, entrySizeVec, UiBase_Absolute, Ui_XY);
  ui_layout_move_dir(c, Ui_Down, spacing, UiBase_Absolute);

  for (u32 row = 0; row != rowCount; ++row) {
    ui_layout_move_to(c, UiBase_Container, UiAlign_TopLeft, Ui_X);
    ui_layout_move_dir(c, Ui_Right, spacing, UiBase_Absolute);

    for (u32 col = 0; col != colCount; ++col) {
      const u32 queueIndex = row * colCount + col;
      if (queueIndex < production->queueCount) {
        hud_production_queue_draw(c, input, production, queueIndex);
      }
      ui_layout_move_dir(c, Ui_Right, entrySize + spacing, UiBase_Absolute);
    }
    ui_layout_move_dir(c, Ui_Down, entrySize + spacing, UiBase_Absolute);
  }

  ui_scrollview_end(c, &hud->productionScrollView);
  ui_layout_pop(c);
}

ecs_system_define(HudDrawSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  CmdControllerComp*             cmd       = ecs_view_write_t(globalItr, CmdControllerComp);
  const InputManagerComp*        input     = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneLevelManagerComp*   level     = ecs_view_read_t(globalItr, SceneLevelManagerComp);
  const SceneSetEnvComp*         setEnv    = ecs_view_read_t(globalItr, SceneSetEnvComp);
  const SceneTerrainComp*        terrain   = ecs_view_read_t(globalItr, SceneTerrainComp);
  const SceneWeaponResourceComp* weaponRes = ecs_view_read_t(globalItr, SceneWeaponResourceComp);

  EcsView* hudView           = ecs_world_view_t(world, HudView);
  EcsView* canvasView        = ecs_world_view_t(world, UiCanvasView);
  EcsView* rendObjView       = ecs_world_view_t(world, RendObjView);
  EcsView* healthView        = ecs_world_view_t(world, HealthView);
  EcsView* infoView          = ecs_world_view_t(world, InfoView);
  EcsView* weaponMapView     = ecs_world_view_t(world, WeaponMapView);
  EcsView* minimapMarkerView = ecs_world_view_t(world, MinimapMarkerView);
  EcsView* productionView    = ecs_world_view_t(world, ProductionView);
  EcsView* visionView        = ecs_world_view_t(world, VisionView);

  EcsIterator* canvasItr     = ecs_view_itr(canvasView);
  EcsIterator* rendObjItr    = ecs_view_itr(rendObjView);
  EcsIterator* infoItr       = ecs_view_itr(infoView);
  EcsIterator* productionItr = ecs_view_itr(productionView);
  EcsIterator* visionItr     = ecs_view_itr(visionView);
  EcsIterator* weaponMapItr  = ecs_view_maybe_at(weaponMapView, scene_weapon_map(weaponRes));

  for (EcsIterator* itr = ecs_view_itr(hudView); ecs_view_walk(itr);) {
    InputStateComp*           inputState = ecs_view_write_t(itr, InputStateComp);
    const SceneCameraComp*    cam        = ecs_view_read_t(itr, SceneCameraComp);
    const SceneTransformComp* camTrans   = ecs_view_read_t(itr, SceneTransformComp);
    HudComp*                  hud        = ecs_view_write_t(itr, HudComp);
    if (!ecs_view_maybe_jump(canvasItr, hud->uiCanvas)) {
      continue;
    }
    UiCanvasComp*   c        = ecs_view_write_t(canvasItr, UiCanvasComp);
    const GeoMatrix viewProj = hud_ui_view_proj(cam, camTrans, c);

    ui_canvas_reset(c);
    if (input_layer_active(input, string_hash_lit("Debug"))) {
      continue;
    }
    const UiVector res = ui_canvas_resolution(c);
    if (UNLIKELY(res.x < f32_epsilon || res.y < f32_epsilon)) {
      continue;
    }
    ui_canvas_to_back(c);

    if (scene_terrain_loaded(terrain)) {
      GeoBox playArea = scene_terrain_play_bounds(terrain);
      playArea.min.y = playArea.max.y = 0; // Draw the play area at height zero.
      hud_indicator_box_draw(hud, rendObjItr, &playArea, ui_color(64, 64, 64, 64));
    }

    hud_minimap_update(hud, rendObjItr, terrain, res);
    hud_level_draw(c, level);

    trace_begin("game_hud_health", TraceColor_White);
    hud_health_draw(c, hud, &viewProj, healthView, res);
    trace_end();

    hud_groups_draw(c, cmd);

    trace_begin("game_hud_minimap", TraceColor_White);
    hud_minimap_draw(c, hud, inputState, terrain, cam, camTrans, minimapMarkerView);
    trace_end();

    if (ecs_view_maybe_jump(visionItr, scene_set_main(setEnv, g_sceneSetSelected))) {
      hud_vision_draw(hud, rendObjItr, visionItr);
    }
    if (ecs_view_maybe_jump(productionItr, scene_set_main(setEnv, g_sceneSetSelected))) {
      hud_production_draw(c, hud, input, rendObjItr, productionItr);
    }

    EcsEntityId  hoveredEntity;
    TimeDuration hoveredTime;
    const bool   hovered = input_hovered_entity(inputState, &hoveredEntity, &hoveredTime);
    if (hovered && hoveredTime >= time_second && ecs_view_maybe_jump(infoItr, hoveredEntity)) {
      hud_info_draw(c, infoItr, weaponMapItr);
    }
    ui_canvas_id_block_next(c); // End on an consistent id.
  }
}

ecs_module_init(game_hud_module) {
  ecs_register_comp(HudComp);

  ecs_register_view(GlobalView);
  ecs_register_view(HudView);
  ecs_register_view(UiCanvasView);
  ecs_register_view(RendObjView);
  ecs_register_view(HealthView);
  ecs_register_view(InfoView);
  ecs_register_view(WeaponMapView);
  ecs_register_view(MinimapMarkerView);
  ecs_register_view(ProductionView);
  ecs_register_view(VisionView);

  ecs_register_system(
      HudDrawSys,
      ecs_view_id(GlobalView),
      ecs_view_id(HudView),
      ecs_view_id(UiCanvasView),
      ecs_view_id(RendObjView),
      ecs_view_id(HealthView),
      ecs_view_id(InfoView),
      ecs_view_id(WeaponMapView),
      ecs_view_id(MinimapMarkerView),
      ecs_view_id(ProductionView),
      ecs_view_id(VisionView));

  ecs_order(HudDrawSys, AppOrder_HudDraw);

  // Initialize product queue action hashes.
  for (u32 i = 0; i != array_elems(g_hudProductQueueActions); ++i) {
    g_hudProductQueueActions[i] = string_hash(fmt_write_scratch("ProductQueue{}", fmt_int(i + 1)));
  }
}

void hud_init(EcsWorld* world, AssetManagerComp* assets, const EcsEntityId cameraEntity) {
  diag_assert_msg(!ecs_world_has_t(world, cameraEntity, HudComp), "HUD already active");

  const EcsEntityId rendObjMinimap = hud_rend_obj_create(
      world, assets, cameraEntity, string_lit("graphics/hud/minimap.graphic"), true);

  const EcsEntityId rendObjIndicatorRing = hud_rend_obj_create(
      world, assets, cameraEntity, string_lit("graphics/hud/indicator_ring.graphic"), false);

  const EcsEntityId rendObjIndicatorBox = hud_rend_obj_create(
      world, assets, cameraEntity, string_lit("graphics/hud/indicator_box.graphic"), false);

  ecs_world_add_t(
      world,
      cameraEntity,
      HudComp,
      .uiCanvas             = ui_canvas_create(world, cameraEntity, UiCanvasCreateFlags_None),
      .rendObjMinimap       = rendObjMinimap,
      .rendObjIndicatorRing = rendObjIndicatorRing,
      .rendObjIndicatorBox  = rendObjIndicatorBox);
}
