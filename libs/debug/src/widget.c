#include "core_float.h"
#include "ui_layout.h"
#include "ui_shape.h"

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
  return debug_widget_editor_vec_resettable_internal(canvas, val, 3, flags);
}

bool debug_widget_editor_color(UiCanvasComp* canvas, GeoColor* val, const UiWidgetFlags flags) {
  return debug_widget_editor_vec_internal(canvas, (GeoVector*)val, 4, flags);
}
