#pragma once
#include "ecs_def.h"

enum {
  UiOrder_Input  = -900,
  UiOrder_Render = 800,
};

/**
 * Register the ecs modules for the Ui library.
 */
void ui_register(EcsDef*);
