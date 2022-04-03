#pragma once
#include "ecs_module.h"
#include "ui_vector.h"

ecs_comp_extern_public(UiStatsComp) {
  UiVector canvasSize;
  u32      canvasCount;
};
