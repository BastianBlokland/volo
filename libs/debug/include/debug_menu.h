#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  DebugMenuEvents_CloseWindow = 1 << 0,
  DebugMenuEvents_Fullscreen  = 1 << 1,
} DebugMenuEvents;

ecs_comp_extern(DebugMenuComp);

DebugMenuEvents debug_menu_events(const DebugMenuComp*);

EcsEntityId debug_menu_create(EcsWorld*, EcsEntityId window);
