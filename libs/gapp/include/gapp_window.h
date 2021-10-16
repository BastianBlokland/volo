#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  GAppWindowEvents_None   = 0,
  GAppWindowEvents_Closed = 1 << 0,
} GAppWindowEvents;

typedef enum {
  GAppWindowFlags_None            = 0,
  GAppWindowFlags_CloseOnInterupt = 1 << 0,

  GAppWindowFlags_Default = GAppWindowFlags_CloseOnInterupt,
} GAppWindowFlags;

ecs_comp_extern(GAppWindowComp);

EcsEntityId      gapp_window_open(EcsWorld*, GAppWindowFlags);
void             gapp_window_close(GAppWindowComp*);
GAppWindowEvents gapp_window_events(const GAppWindowComp*);
