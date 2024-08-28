#pragma once
#include "ecs_module.h"

typedef enum {
  VfxStat_ParticleCount,
  VfxStat_SpriteCount,
  VfxStat_LightCount,
  VfxStat_StampCount,

  VfxStat_Count,
} VfxStat;

typedef struct {
  i32 valuesAccum[VfxStat_Count];
  i32 valuesLast[VfxStat_Count];
} VfxStatSet;

ecs_comp_extern_public(VfxStatsComp) {
  i32 valuesAccum[VfxStat_Count];
  i32 valuesLast[VfxStat_Count];
};

ecs_comp_extern_public(VfxStatsGlobalComp) { i32 values[VfxStat_Count]; };

String vfx_stat_name(VfxStat);
i32    vfx_stat_get(const VfxStatSet*, VfxStat);
void   vfx_stat_report(VfxStatSet*, VfxStat);
void   vfx_stat_combine(VfxStatSet*, const VfxStatSet*);
