#include "asset_prefab.h"
#include "core_array.h"
#include "core_float.h"
#include "core_format.h"
#include "core_stringtable.h"
#include "dev_finder.h"
#include "dev_widget.h"
#include "ecs_entity.h"
#include "geo_vector.h"
#include "scene_faction.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_widget.h"

static const String g_tooltipReset        = string_static("Reset the value to default.");
static const String g_tooltipAssetRefresh = string_static("Refresh the asset query.");

static UiColor debug_geo_to_ui_color(const GeoColor color) {
  const GeoColor clamped = geo_color_clamp01(color);
  return (UiColor){
      .r = (u8)(clamped.r * 255.999f),
      .g = (u8)(clamped.g * 255.999f),
      .b = (u8)(clamped.b * 255.999f),
      .a = (u8)(clamped.a * 255.999f),
  };
}

bool debug_widget_f32(UiCanvasComp* canvas, f32* val, const UiWidgetFlags flags) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .min = f32_min, .max = f32_max, .flags = flags)) {
    *val = (f32)v;
    return true;
  }
  return false;
}

bool debug_widget_f32_many(
    UiCanvasComp* canvas, f32* val, const u32 count, const UiWidgetFlags flags) {
  if (!count) {
    return false;
  }
  if (count == 1) {
    return debug_widget_f32(canvas, val, flags);
  }
  static const f32 g_spacing   = 10.0f;
  const u8         numSpacings = count - 1;
  const UiAlign    align       = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / count, 0), UiBase_Current, Ui_X);
  ui_layout_grow(
      canvas, align, ui_vector(numSpacings * -g_spacing / count, 0), UiBase_Absolute, Ui_X);

  bool isDirty = false;
  for (u32 i = 0; i != count; ++i) {
    isDirty |= debug_widget_f32(canvas, val + i, flags);
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
  ui_layout_pop(canvas);
  return isDirty;
}

bool debug_widget_f32_many_resettable(
    UiCanvasComp*       canvas,
    f32*                val,
    const u32           count,
    const f32           defaultVal,
    const UiWidgetFlags flags) {
  ui_layout_push(canvas);
  ui_layout_grow(canvas, UiAlign_MiddleLeft, ui_vector(-30, 0), UiBase_Absolute, Ui_X);
  bool isDirty = debug_widget_f32_many(canvas, val, count, flags);
  ui_layout_next(canvas, Ui_Right, 8);
  ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(22, 0), UiBase_Absolute, Ui_X);
  if (ui_button(canvas, .label = ui_shape_scratch(UiShape_Default), .tooltip = g_tooltipReset)) {
    for (u32 i = 0; i != count; ++i) {
      val[i] = defaultVal;
    }
    isDirty = true;
  }
  ui_layout_pop(canvas);
  return isDirty;
}

bool debug_widget_u16(UiCanvasComp* canvas, u16* val, const UiWidgetFlags flags) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .max = u16_max, .step = 1, .flags = flags)) {
    *val = (u16)v;
    return true;
  }
  return false;
}

bool debug_widget_u32(UiCanvasComp* canvas, u32* val, const UiWidgetFlags flags) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .max = u32_max, .step = 1, .flags = flags)) {
    *val = (u32)v;
    return true;
  }
  return false;
}

bool debug_widget_vec3(UiCanvasComp* canvas, GeoVector* val, const UiWidgetFlags flags) {
  return debug_widget_f32_many(canvas, val->comps, 3, flags);
}

bool debug_widget_vec4(UiCanvasComp* canvas, GeoVector* val, const UiWidgetFlags flags) {
  return debug_widget_f32_many(canvas, val->comps, 4, flags);
}

bool debug_widget_vec3_resettable(UiCanvasComp* canvas, GeoVector* val, const UiWidgetFlags flags) {
  return debug_widget_f32_many_resettable(canvas, val->comps, 3, 0.0f /* default */, flags);
}

bool debug_widget_vec4_resettable(UiCanvasComp* canvas, GeoVector* val, const UiWidgetFlags flags) {
  return debug_widget_f32_many_resettable(canvas, val->comps, 4, 0.0f /* default */, flags);
}

bool debug_widget_quat(UiCanvasComp* canvas, GeoQuat* val, const UiWidgetFlags flags) {
  if (debug_widget_f32_many_resettable(canvas, val->comps, 4, 0.0f /* default */, flags)) {
    *val = geo_quat_norm_or_ident(*val);
    return true;
  }
  return false;
}

bool debug_widget_color(UiCanvasComp* canvas, GeoColor* val, const UiWidgetFlags flags) {
  ui_layout_push(canvas);
  ui_layout_grow(canvas, UiAlign_MiddleLeft, ui_vector(-30, 0), UiBase_Absolute, Ui_X);
  bool isDirty = debug_widget_f32_many(canvas, val->data, 4 /* count */, flags);
  ui_layout_next(canvas, Ui_Right, 8);
  ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(22, 0), UiBase_Absolute, Ui_X);

  ui_style_push(canvas);
  ui_style_outline(canvas, 4);
  ui_style_color(canvas, debug_geo_to_ui_color(*val));
  const UiId preview = ui_canvas_draw_glyph(canvas, UiShape_Circle, 0, UiFlags_Interactable);
  ui_tooltip(canvas, preview, string_lit("Color preview."));
  ui_style_pop(canvas);

  ui_layout_pop(canvas);
  return isDirty;
}

bool debug_widget_faction(UiCanvasComp* c, SceneFaction* val, const UiWidgetFlags flags) {
  static const String g_names[] = {
      string_static("None"),
      string_static("A"),
      string_static("B"),
      string_static("C"),
      string_static("D"),
  };
  static const SceneFaction g_values[] = {
      SceneFaction_None,
      SceneFaction_A,
      SceneFaction_B,
      SceneFaction_C,
      SceneFaction_D,
  };
  ASSERT(array_elems(g_names) == array_elems(g_values), "Mismatching faction options");

  i32 index = 0;
  for (u32 i = 0; i != array_elems(g_values); ++i) {
    if (g_values[i] == *val) {
      index = i;
      break;
    }
  }
  if (ui_select(c, &index, g_names, array_elems(g_values), .flags = flags)) {
    *val = g_values[index];
    return true;
  }
  return false;
}

bool debug_widget_prefab(
    UiCanvasComp* c, const AssetPrefabMapComp* map, StringHash* val, const UiWidgetFlags flags) {
  if (!map) {
    const String name = stringtable_lookup(g_stringtable, *val);
    if (string_is_empty(name)) {
      ui_label(c, string_lit("< unknown >"));
    } else {
      ui_label(c, name, .selectable = true);
    }
    return false;
  }

  const u16 currentPrefabIndex = asset_prefab_find_index(map, *val);

  i32 userIndex = -1;
  if (!sentinel_check(currentPrefabIndex)) {
    userIndex = asset_prefab_index_to_user(map, currentPrefabIndex);
  }
  if (ui_select(c, &userIndex, map->userNames, (u32)map->prefabCount, .flags = flags)) {
    *val = map->prefabs[asset_prefab_index_from_user(map, userIndex)].name;
    return true;
  }
  return false;
}

bool debug_widget_asset(
    UiCanvasComp*             c,
    DebugFinderComp*          finder,
    const DebugFinderCategory cat,
    EcsEntityId*              val,
    const UiWidgetFlags       flags) {

  const DebugFinderResult entries = debug_finder_get(finder, cat);
  ui_layout_push(c);
  ui_layout_grow(c, UiAlign_MiddleLeft, ui_vector(-30, 0), UiBase_Absolute, Ui_X);

  bool changed = false;
  if (entries.status != DebugFinderStatus_Ready) {
    ui_label(c, string_lit("Loading..."));
  } else {
    i32 index = -1;
    for (u32 i = 0; i != entries.count; ++i) {
      if (entries.entities[i] == *val) {
        index = (i32)i;
        break;
      }
    }
    const String tooltip = fmt_write_scratch(
        "Id:\a>0B{}\n"
        "Entity:\a>0B{}\n",
        fmt_text(index < 0 ? string_lit("< None >") : entries.ids[index]),
        ecs_entity_fmt(*val));

    if (ui_select(
            c,
            &index,
            entries.ids,
            entries.count,
            .allowNone = true,
            .flags     = flags,
            .tooltip   = tooltip)) {
      *val    = index < 0 ? ecs_entity_invalid : entries.entities[index];
      changed = true;
    }
  }

  bool refresh = false;
  ui_layout_next(c, Ui_Right, 8);
  ui_layout_resize(c, UiAlign_MiddleLeft, ui_vector(22, 0), UiBase_Absolute, Ui_X);
  if (ui_button(c, .label = ui_shape_scratch(UiShape_Restart), .tooltip = g_tooltipAssetRefresh)) {
    refresh = true;
  }
  debug_finder_query(finder, cat, refresh);

  ui_layout_pop(c);

  ui_canvas_id_block_next(c); // End on a consistent id.
  return changed;
}
