#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the ui library.
 */

ecs_comp_extern(UiCanvasComp);
ecs_comp_extern(UiSettingsGlobalComp);
ecs_comp_extern(UiStatsComp);

typedef enum eUiAlign              UiAlign;
typedef enum eUiAxis               UiAxis;
typedef enum eUiBase               UiBase;
typedef enum eUiCanvasCreateFlags  UiCanvasCreateFlags;
typedef enum eUiClip               UiClip;
typedef enum eUiDir                UiDir;
typedef enum eUiFlags              UiFlags;
typedef enum eUiInteractType       UiInteractType;
typedef enum eUiLayer              UiLayer;
typedef enum eUiPanelFlags         UiPanelFlags;
typedef enum eUiPersistentFlags    UiPersistentFlags;
typedef enum eUiScrollviewFlags    UiScrollviewFlags;
typedef enum eUiSettingGlobalFlags UiSettingGlobalFlags;
typedef enum eUiSoundType          UiSoundType;
typedef enum eUiStatus             UiStatus;
typedef enum eUiTextFilter         UiTextFilter;
typedef enum eUiWeight             UiWeight;
typedef struct sUiPanel            UiPanel;
typedef struct sUiScrollview       UiScrollview;
typedef struct sUiTable            UiTable;
typedef u64                        UiId;
typedef union uUiColor             UiColor;
typedef union uUiRect              UiRect;
typedef union uUiVector            UiVector;
