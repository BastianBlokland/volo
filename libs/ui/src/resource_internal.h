#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  UiAtlasType_Font,
  UiAtlasType_Image,

  UiAtlasType_Count,
} UiAtlasType;

ecs_comp_extern(UiGlobalResourcesComp);

EcsEntityId ui_resource_atlas(const UiGlobalResourcesComp*, UiAtlasType);
EcsEntityId ui_resource_graphic(const UiGlobalResourcesComp*);
EcsEntityId ui_resource_graphic_debug(const UiGlobalResourcesComp*);
EcsEntityId ui_resource_sound_click(const UiGlobalResourcesComp*);
EcsEntityId ui_resource_sound_click_alt(const UiGlobalResourcesComp*);
