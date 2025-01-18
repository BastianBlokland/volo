#pragma once
#include "asset.h"
#include "dev.h"
#include "geo.h"
#include "scene.h"
#include "ui.h"

// clang-format off

bool debug_widget_f32(UiCanvasComp*, f32* val, UiWidgetFlags);
bool debug_widget_f32_many(UiCanvasComp*, f32* val, u32 count, UiWidgetFlags);
bool debug_widget_f32_many_resettable(UiCanvasComp*, f32* val, u32 count, f32 defaultVal, UiWidgetFlags);
bool debug_widget_u16(UiCanvasComp*, u16* val, UiWidgetFlags);
bool debug_widget_u32(UiCanvasComp*, u32* val, UiWidgetFlags);
bool debug_widget_vec3(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_vec4(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_vec3_resettable(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_vec4_resettable(UiCanvasComp*, GeoVector* val, UiWidgetFlags);
bool debug_widget_quat(UiCanvasComp*, GeoQuat* val, UiWidgetFlags);
bool debug_widget_color(UiCanvasComp*, GeoColor* val, UiWidgetFlags);
bool debug_widget_faction(UiCanvasComp*, SceneFaction*, UiWidgetFlags);
bool debug_widget_prefab(UiCanvasComp*, const AssetPrefabMapComp*, StringHash*, UiWidgetFlags);
bool debug_widget_asset(UiCanvasComp*, DebugFinderComp*, DebugFinderCategory, EcsEntityId*, UiWidgetFlags);

// clang-format on
