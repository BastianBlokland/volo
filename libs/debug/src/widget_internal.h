#pragma once
#include "asset.h"
#include "debug.h"
#include "geo.h"
#include "scene.h"
#include "ui.h"

// clang-format off

bool debug_widget_editor_f32(UiCanvasComp*, f32* val, UiWidgetFlags);
bool debug_widget_editor_u16(UiCanvasComp*, u16* val, UiWidgetFlags);
bool debug_widget_editor_u32(UiCanvasComp*, u32* val, UiWidgetFlags);
bool debug_widget_editor_vec3(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_editor_vec4(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_editor_vec3_resettable(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_editor_vec4_resettable(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_editor_quat(UiCanvasComp*, GeoQuat* val, UiWidgetFlags);
bool debug_widget_editor_color(UiCanvasComp*, GeoColor* val, UiWidgetFlags);
bool debug_widget_editor_faction(UiCanvasComp*, SceneFaction*, UiWidgetFlags);
bool debug_widget_editor_prefab(UiCanvasComp*, const AssetPrefabMapComp*, StringHash*, UiWidgetFlags);
bool debug_widget_editor_asset(UiCanvasComp*, DebugFinderComp*, DebugFinderCategory, EcsEntityId*, UiWidgetFlags);

// clang-format on
