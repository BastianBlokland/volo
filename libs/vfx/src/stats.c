#include "vfx_stats.h"

ecs_comp_define_public(VfxStatsComp);

static void ecs_combine_vfx_stats(void* dataA, void* dataB) {
  VfxStatsComp* compA = dataA;
  VfxStatsComp* compB = dataB;

  for (VfxStat stat = 0; stat != VfxStat_Count; ++stat) {
    compA->valuesNew[stat] += compB->valuesNew[stat];
    compA->valuesLast[stat] += compB->valuesLast[stat];
  }
}

ecs_module_init(vfx_stats_module) {
  ecs_register_comp(VfxStatsComp, .combinator = ecs_combine_vfx_stats);
}
