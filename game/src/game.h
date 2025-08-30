#pragma once
#include "ecs/module.h"

enum {
  GameOrder_StateUpdate = -2,
};

typedef enum {
  GameState_None,
  GameState_MenuMain,
  GameState_MenuSelect,
  GameState_Loading,
  GameState_Play,
  GameState_Edit,
  GameState_Pause,

  GameState_Count,
} GameState;

ecs_comp_extern(GameComp);

GameState game_state(const GameComp*);
