#pragma once
#include "ecs_def.h"

enum {
  UiOrder_Render = 800,
};

/**
 * Register the ecs modules for the Ui library.
 */
void ui_register(EcsDef*);
