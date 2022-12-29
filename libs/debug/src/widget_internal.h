#pragma once
#include "geo_color.h"
#include "geo_vector.h"
#include "ui_widget.h"

bool debug_widget_editor_f32(UiCanvasComp*, f32* val, UiWidgetFlags);
bool debug_widget_editor_u16(UiCanvasComp*, u16* val, UiWidgetFlags);
bool debug_widget_editor_u32(UiCanvasComp*, u32* val, UiWidgetFlags);
bool debug_widget_editor_vec3(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_editor_vec4(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_editor_vec3_resettable(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_editor_vec4_resettable(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_editor_color(UiCanvasComp*, GeoColor* val, UiWidgetFlags);
