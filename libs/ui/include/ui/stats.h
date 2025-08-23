#pragma once
#include "ecs/module.h"
#include "ui/vector.h"

ecs_comp_extern_public(UiStatsComp) {
  UiVector canvasSize;
  u32      canvasCount;
  u32      trackedElemCount, persistElemCount;
  u32      atomCount, atomDeferredCount;
  u32      clipRectCount;
  u32      commandCount;
};
