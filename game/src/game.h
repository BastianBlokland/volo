#pragma once
#include "ecs/module.h"

typedef enum {
  GameState_MenuMain,
  GameState_MenuSelect,
  GameState_Loading,
  GameState_Play,
  GameState_Debug,
  GameState_Edit,
  GameState_Pause,
} GameState;

ecs_comp_extern(GameComp);

GameState game_state(const GameComp*);
