#pragma once
#include "ecs/module.h"

enum {
  GameOrder_StateUpdate = -2,
};

typedef enum {
  GameState_None,
  GameState_MenuMain,
  GameState_MenuSelect,
  GameState_MenuCredits,
  GameState_Loading,
  GameState_Play,
  GameState_Edit,
  GameState_Pause,
  GameState_Result,

  GameState_Count,
} GameState;

ecs_comp_extern(GameComp);

GameState game_state(const GameComp*);
