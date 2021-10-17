#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  GapWindowEvents_None           = 0,
  GapWindowEvents_Resized        = 1 << 1,
  GapWindowEvents_CloseRequested = 1 << 2,
  GapWindowEvents_Closed         = 1 << 3,
  GapWindowEvents_TitleUpdated   = 1 << 4,
} GapWindowEvents;

typedef enum {
  GapWindowFlags_None            = 0,
  GapWindowFlags_CloseOnInterupt = 1 << 0,
  GapWindowFlags_CloseOnRequest  = 1 << 1,

  GapWindowFlags_Default = GapWindowFlags_CloseOnInterupt | GapWindowFlags_CloseOnRequest,
} GapWindowFlags;

ecs_comp_extern(GapWindowComp);

EcsEntityId     gap_window_open(EcsWorld*, GapWindowFlags, u32 width, u32 height);
void            gap_window_close(GapWindowComp*);
GapWindowEvents gap_window_events(const GapWindowComp*);
String          gap_window_title_get(GapWindowComp*);
void            gap_window_title_set(GapWindowComp*, String newTitle);
u32             gap_window_width(const GapWindowComp*);
u32             gap_window_height(const GapWindowComp*);
