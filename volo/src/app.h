#pragma once
#include "ecs/module.h"

typedef enum {
  AppState_MenuMain,
  AppState_MenuLevel,
  AppState_Play,
  AppState_Edit,
  AppState_Pause,
} AppState;

ecs_comp_extern(AppComp);

AppState app_state(const AppComp*);
