#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  GapWindowEvents_None   = 0,
  GapWindowEvents_Closed = 1 << 0,
} GapWindowEvents;

typedef enum {
  GapWindowFlags_None            = 0,
  GapWindowFlags_CloseOnInterupt = 1 << 0,

  GapWindowFlags_Default = GapWindowFlags_CloseOnInterupt,
} GapWindowFlags;

ecs_comp_extern(GapWindowComp);

EcsEntityId     gap_window_open(EcsWorld*, GapWindowFlags);
void            gap_window_close(GapWindowComp*);
GapWindowEvents gap_window_events(const GapWindowComp*);
