#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "gap_input.h"
#include "gap_vector.h"

typedef enum {
  GapWindowEvents_Resized        = 1 << 0,
  GapWindowEvents_KeyPressed     = 1 << 1,
  GapWindowEvents_KeyReleased    = 1 << 2,
  GapWindowEvents_TitleUpdated   = 1 << 3,
  GapWindowEvents_CloseRequested = 1 << 4,
  GapWindowEvents_Closed         = 1 << 5,
} GapWindowEvents;

typedef enum {
  GapWindowFlags_None            = 0,
  GapWindowFlags_CloseOnInterupt = 1 << 0,
  GapWindowFlags_CloseOnRequest  = 1 << 1,

  GapWindowFlags_Default = GapWindowFlags_CloseOnInterupt | GapWindowFlags_CloseOnRequest,
} GapWindowFlags;

typedef enum {
  GapWindowMode_Windowed   = 0,
  GapWindowMode_Fullscreen = 1,
} GapWindowMode;

ecs_comp_extern(GapWindowComp);

EcsEntityId     gap_window_open(EcsWorld*, GapWindowFlags, GapVector size);
void            gap_window_close(GapWindowComp*);
GapWindowEvents gap_window_events(const GapWindowComp*);
GapWindowMode   gap_window_mode(const GapWindowComp*);
void            gap_window_resize(GapWindowComp*, GapVector size, GapWindowMode);
String          gap_window_title_get(const GapWindowComp*);
void            gap_window_title_set(GapWindowComp*, String newTitle);
GapVector       gap_window_param(const GapWindowComp*, GapParam);
bool            gap_window_key_pressed(const GapWindowComp*, GapKey);
bool            gap_window_key_released(const GapWindowComp*, GapKey);
bool            gap_window_key_down(const GapWindowComp*, GapKey);
