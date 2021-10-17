#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef u32 GAppWindowId;

ecs_comp_extern(GAppPlatformComp);

EcsEntityId  gapp_platform_create(EcsWorld*);
GAppWindowId gapp_platform_window_create(GAppPlatformComp*, u32 width, u32 height);
void         gapp_platform_window_destroy(GAppPlatformComp*, GAppWindowId);
