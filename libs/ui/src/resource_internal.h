#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  UiAtlasRes_Font,
  UiAtlasRes_Image,

  UiAtlasRes_Count,
} UiAtlasRes;

typedef enum {
  UiSoundRes_Click,
  UiSoundRes_ClickAlt,

  UiSoundRes_Count
} UiSoundRes;

ecs_comp_extern(UiGlobalResourcesComp);

EcsEntityId ui_resource_atlas(const UiGlobalResourcesComp*, UiAtlasRes);
EcsEntityId ui_resource_graphic(const UiGlobalResourcesComp*);
EcsEntityId ui_resource_graphic_debug(const UiGlobalResourcesComp*);
EcsEntityId ui_resource_sound(const UiGlobalResourcesComp*, UiSoundRes);
