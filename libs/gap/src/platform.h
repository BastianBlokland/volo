#pragma once
#include "ecs/module.h"

#include "pal.h"

typedef struct {
  EcsEntityId iconAsset;
  bool        loading;
} GapPlatformIcon;

ecs_comp_extern_public(GapPlatformComp) {
  GapPal*         pal;
  GapPlatformIcon icons[GapIcon_Count];
  GapPlatformIcon cursors[GapCursor_Count];
};
