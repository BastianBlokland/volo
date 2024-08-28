#include "ecs_view.h"
#include "ecs_world.h"
#include "vfx_register.h"
#include "vfx_stats.h"

ecs_comp_define_public(VfxStatsComp);
ecs_comp_define_public(VfxStatsGlobalComp);

static void ecs_combine_vfx_stats(void* dataA, void* dataB) {
  VfxStatsComp* compA = dataA;
  VfxStatsComp* compB = dataB;

  for (VfxStat stat = 0; stat != VfxStat_Count; ++stat) {
    compA->valuesAccum[stat] += compB->valuesAccum[stat];
    compA->valuesLast[stat] += compB->valuesLast[stat];
  }
}

ecs_view_define(GlobalStatsView) { ecs_access_write(VfxStatsGlobalComp); }
ecs_view_define(StatsView) { ecs_access_write(VfxStatsComp); }

static VfxStatsGlobalComp* vfx_stats_global_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, GlobalStatsView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  return itr ? ecs_view_write_t(itr, VfxStatsGlobalComp)
             : ecs_world_add_t(world, ecs_world_global(world), VfxStatsGlobalComp);
}

ecs_system_define(VfxStatsUpdateSys) {
  VfxStatsGlobalComp* globalStats = vfx_stats_global_get_or_create(world);

  mem_set(mem_var(globalStats->values), 0);

  EcsView* statsView = ecs_world_view_t(world, StatsView);
  for (EcsIterator* itr = ecs_view_itr(statsView); ecs_view_walk(itr);) {
    VfxStatsComp* stats = ecs_view_write_t(itr, VfxStatsComp);

    for (VfxStat stat = 0; stat != VfxStat_Count; ++stat) {
      globalStats->values[stat] += stats->valuesAccum[stat];

      stats->valuesLast[stat]  = stats->valuesAccum[stat];
      stats->valuesAccum[stat] = 0;
    }
  }
}

ecs_module_init(vfx_stats_module) {
  ecs_register_comp(VfxStatsComp, .combinator = ecs_combine_vfx_stats);
  ecs_register_comp(VfxStatsGlobalComp);

  ecs_register_view(GlobalStatsView);
  ecs_register_view(StatsView);

  ecs_register_system(VfxStatsUpdateSys, ecs_view_id(GlobalStatsView), ecs_view_id(StatsView));

  ecs_order(VfxStatsUpdateSys, VfxOrder_StatCollect);
}

String vfx_stat_name(const VfxStat stat) {
  static const String g_names[VfxStat_Count] = {
      [VfxStat_ParticleCount] = string_static("Particles"),
      [VfxStat_SpriteCount]   = string_static("Sprites"),
      [VfxStat_LightCount]    = string_static("Lights"),
      [VfxStat_StampCount]    = string_static("Stamps"),
  };
  return g_names[stat];
}

i32 vfx_stat_get(const VfxStatSet* set, const VfxStat stat) { return set->valuesLast[stat]; }

void vfx_stat_report(VfxStatSet* set, const VfxStat stat) { ++set->valuesAccum[stat]; }

void vfx_stat_combine(VfxStatSet* a, const VfxStatSet* b) {
  for (VfxStat stat = 0; stat != VfxStat_Count; ++stat) {
    a->valuesAccum[stat] += b->valuesAccum[stat];
    a->valuesLast[stat] += b->valuesLast[stat];
  }
}
