#pragma once
#include "ecs_module.h"

typedef enum {
  VfxStat_SpriteCount,
  VfxStat_StampCount,
  VfxStat_LightCount,

  VfxStat_Count,
} VfxStat;

ecs_comp_extern_public(VfxStatsComp) {
  i32 valuesNew[VfxStat_Count];
  i32 valuesLast[VfxStat_Count];
};
