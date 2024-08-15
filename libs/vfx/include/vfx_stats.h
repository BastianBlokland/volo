#pragma once
#include "ecs_module.h"

typedef enum {
  VfxStat_ParticleCount,
  VfxStat_SpriteCount,
  VfxStat_LightCount,
  VfxStat_StampCount,

  VfxStat_Count,
} VfxStat;

ecs_comp_extern_public(VfxStatsComp) {
  i32 valuesNew[VfxStat_Count];
  i32 valuesLast[VfxStat_Count];
};

ecs_comp_extern_public(VfxStatsGlobalComp) { i32 values[VfxStat_Count]; };

String vfx_stat_name(VfxStat);
