#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  GappWindowEvents_None   = 0,
  GappWindowEvents_Closed = 1 << 0,
} GappWindowEvents;

typedef enum {
  GappWindowFlags_None            = 0,
  GappWindowFlags_CloseOnInterupt = 1 << 0,

  GappWindowFlags_Default = GappWindowFlags_CloseOnInterupt,
} GappWindowFlags;

ecs_comp_extern(GappWindow);

EcsEntityId      gapp_window_open(EcsWorld*, GappWindowFlags);
void             gapp_window_close(GappWindow*);
GappWindowEvents gapp_window_events(const GappWindow*);
