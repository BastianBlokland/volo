#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef u32 GapWindowId;

ecs_comp_extern(GapPlatformComp);

EcsEntityId gap_platform_create(EcsWorld*);
GapWindowId gap_platform_window_create(GapPlatformComp*, u32 width, u32 height);
void        gap_platform_window_destroy(GapPlatformComp*, GapWindowId);
