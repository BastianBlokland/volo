#include "asset_prefab.h"
#include "core_array.h"
#include "core_float.h"
#include "core_stringtable.h"
#include "geo_vector.h"
#include "scene_faction.h"
#include "ui_layout.h"
#include "ui_shape.h"
#include "ui_widget.h"

#include "widget_internal.h"

static const String g_tooltipReset = string_static("Reset the value to default.");

bool debug_widget_editor_f32(UiCanvasComp* canvas, f32* val, const UiWidgetFlags flags) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .min = f32_min, .max = f32_max, .flags = flags)) {
    *val = (f32)v;
    return true;
  }
  return false;
}

bool debug_widget_editor_u16(UiCanvasComp* canvas, u16* val, const UiWidgetFlags flags) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .max = u16_max, .step = 1, .flags = flags)) {
    *val = (u16)v;
    return true;
  }
  return false;
}

bool debug_widget_editor_u32(UiCanvasComp* canvas, u32* val, const UiWidgetFlags flags) {
  f64 v = *val;
  if (ui_numbox(canvas, &v, .max = u32_max, .step = 1, .flags = flags)) {
    *val = (u32)v;
    return true;
  }
  return false;
}

static bool debug_widget_editor_vec_internal(
    UiCanvasComp* canvas, GeoVector* val, const u8 numComps, const UiWidgetFlags flags) {
  static const f32 g_spacing   = 10.0f;
  const u8         numSpacings = numComps - 1;
  const UiAlign    align       = UiAlign_MiddleLeft;
  ui_layout_push(canvas);
  ui_layout_resize(canvas, align, ui_vector(1.0f / numComps, 0), UiBase_Current, Ui_X);
  ui_layout_grow(
      canvas, align, ui_vector(numSpacings * -g_spacing / numComps, 0), UiBase_Absolute, Ui_X);

  bool isDirty = false;
  for (u8 comp = 0; comp != numComps; ++comp) {
    isDirty |= debug_widget_editor_f32(canvas, &val->comps[comp], flags);
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
  ui_layout_pop(canvas);
  return isDirty;
}

bool debug_widget_editor_vec3(UiCanvasComp* canvas, GeoVector* val, const UiWidgetFlags flags) {
  return debug_widget_editor_vec_internal(canvas, val, 3, flags);
}

bool debug_widget_editor_vec4(UiCanvasComp* canvas, GeoVector* val, const UiWidgetFlags flags) {
  return debug_widget_editor_vec_internal(canvas, val, 4, flags);
}

static bool debug_widget_editor_vec_resettable_internal(
    UiCanvasComp* canvas, GeoVector* val, const u8 numComps, const UiWidgetFlags flags) {
  ui_layout_push(canvas);
  ui_layout_grow(canvas, UiAlign_MiddleLeft, ui_vector(-30, 0), UiBase_Absolute, Ui_X);
  bool isDirty = debug_widget_editor_vec_internal(canvas, val, numComps, flags);
  ui_layout_next(canvas, Ui_Right, 8);
  ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(22, 0), UiBase_Absolute, Ui_X);
  if (ui_button(canvas, .label = ui_shape_scratch(UiShape_Default), .tooltip = g_tooltipReset)) {
    for (u8 comp = 0; comp != numComps; ++comp) {
      val->comps[comp] = 0;
    }
    isDirty = true;
  }
  ui_layout_pop(canvas);
  return isDirty;
}

bool debug_widget_editor_vec3_resettable(
    UiCanvasComp* canvas, GeoVector* val, const UiWidgetFlags flags) {
  return debug_widget_editor_vec_resettable_internal(canvas, val, 3, flags);
}

bool debug_widget_editor_vec4_resettable(
    UiCanvasComp* canvas, GeoVector* val, const UiWidgetFlags flags) {
  return debug_widget_editor_vec_resettable_internal(canvas, val, 4, flags);
}

bool debug_widget_editor_quat(UiCanvasComp* canvas, GeoQuat* val, const UiWidgetFlags flags) {
  if (debug_widget_editor_vec_resettable_internal(canvas, (GeoVector*)val, 4, flags)) {
    *val = geo_quat_norm_or_ident(*val);
    return true;
  }
  return false;
}

bool debug_widget_editor_color(UiCanvasComp* canvas, GeoColor* val, const UiWidgetFlags flags) {
  return debug_widget_editor_vec_internal(canvas, (GeoVector*)val, 4, flags);
}

bool debug_widget_editor_faction(UiCanvasComp* c, SceneFaction* val, const UiWidgetFlags flags) {
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

bool debug_widget_editor_prefab(
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
