#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#include "pal_internal.h"

typedef struct {
  EcsEntityId asset;
} GapPlatformCursor;

ecs_comp_extern_public(GapPlatformComp) {
  GapPal*           pal;
  GapPlatformCursor cursors[GapCursor_Count];
};
