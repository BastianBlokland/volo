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

ecs_comp_extern_public(VfxStatsAnyComp); // On any entity with vfx stats.
ecs_comp_extern_public(VfxStatsGlobalComp) { VfxStatSet set; };

String vfx_stat_name(VfxStat);
i32    vfx_stat_get(const VfxStatSet*, VfxStat);
void   vfx_stat_report(VfxStatSet*, VfxStat);
void   vfx_stat_clear(VfxStatSet*);
void   vfx_stat_combine(VfxStatSet*, const VfxStatSet*);
