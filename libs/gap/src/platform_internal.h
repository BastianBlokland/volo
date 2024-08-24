#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#include "pal_internal.h"

typedef struct {
  EcsEntityId iconAsset;
  bool        loading;
} GapPlatformIcon;

ecs_comp_extern_public(GapPlatformComp) {
  GapPal*         pal;
  GapPlatformIcon cursors[GapCursor_Count];
};
