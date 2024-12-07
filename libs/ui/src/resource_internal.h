#pragma once
#include "ecs_module.h"

typedef enum {
  UiAtlasRes_Font,
  UiAtlasRes_Image,

  UiAtlasRes_Count,
} UiAtlasRes;

typedef enum {
  UiGraphicRes_Normal,
  UiGraphicRes_Debug,

  UiGraphicRes_Count,
} UiGraphicRes;

typedef enum {
  UiSoundRes_Click,
  UiSoundRes_ClickAlt,

  UiSoundRes_Count
} UiSoundRes;

ecs_comp_extern(UiGlobalResourcesComp);

EcsEntityId ui_resource_atlas(const UiGlobalResourcesComp*, UiAtlasRes);
EcsEntityId ui_resource_graphic(const UiGlobalResourcesComp*, UiGraphicRes);
EcsEntityId ui_resource_sound(const UiGlobalResourcesComp*, UiSoundRes);
