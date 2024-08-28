#include "ecs_view.h"
#include "ecs_world.h"
#include "vfx_decal.h"
#include "vfx_register.h"
#include "vfx_stats.h"
#include "vfx_system.h"

ecs_comp_define_public(VfxStatsAnyComp);
ecs_comp_define_public(VfxStatsGlobalComp);

ecs_view_define(GlobalStatsView) { ecs_access_write(VfxStatsGlobalComp); }

ecs_view_define(StatsView) {
  ecs_access_with(VfxStatsAnyComp);
  ecs_access_maybe_write(VfxSystemStatsComp);
  ecs_access_maybe_write(VfxDecalSingleStatsComp);
  ecs_access_maybe_write(VfxDecalTrailStatsComp);
}

static VfxStatsGlobalComp* vfx_stats_global_get_or_create(EcsWorld* world) {
  EcsView*     view = ecs_world_view_t(world, GlobalStatsView);
  EcsIterator* itr  = ecs_view_maybe_at(view, ecs_world_global(world));
  return itr ? ecs_view_write_t(itr, VfxStatsGlobalComp)
             : ecs_world_add_t(world, ecs_world_global(world), VfxStatsGlobalComp);
}

static void vfx_stats_flush(VfxStatsGlobalComp* global, VfxStatSet* set) {
  for (VfxStat stat = 0; stat != VfxStat_Count; ++stat) {
    global->set.valuesLast[stat] += set->valuesAccum[stat];

    set->valuesLast[stat]  = set->valuesAccum[stat];
    set->valuesAccum[stat] = 0;
  }
}

ecs_system_define(VfxStatsUpdateSys) {
  VfxStatsGlobalComp* globalStats = vfx_stats_global_get_or_create(world);

  vfx_stat_clear(&globalStats->set);

  EcsView* statsView = ecs_world_view_t(world, StatsView);
  for (EcsIterator* itr = ecs_view_itr(statsView); ecs_view_walk(itr);) {
    VfxSystemStatsComp* systemStats = ecs_view_write_t(itr, VfxSystemStatsComp);
    if (systemStats) {
      vfx_stats_flush(globalStats, &systemStats->set);
    }

    VfxDecalSingleStatsComp* decalSingleStats = ecs_view_write_t(itr, VfxDecalSingleStatsComp);
    if (decalSingleStats) {
      vfx_stats_flush(globalStats, &decalSingleStats->set);
    }

    VfxDecalTrailStatsComp* decalTrailStats = ecs_view_write_t(itr, VfxDecalTrailStatsComp);
    if (decalTrailStats) {
      vfx_stats_flush(globalStats, &decalTrailStats->set);
    }
  }
}

ecs_module_init(vfx_stats_module) {
  ecs_register_comp_empty(VfxStatsAnyComp);
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

void vfx_stat_clear(VfxStatSet* set) {
  mem_set(mem_var(set->valuesAccum), 0);
  mem_set(mem_var(set->valuesLast), 0);
}

void vfx_stat_combine(VfxStatSet* a, const VfxStatSet* b) {
  for (VfxStat stat = 0; stat != VfxStat_Count; ++stat) {
    a->valuesAccum[stat] += b->valuesAccum[stat];
    a->valuesLast[stat] += b->valuesLast[stat];
  }
}
