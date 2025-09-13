#include "core/array.h"
#include "core/diag.h"
#include "ecs/world.h"
#include "scene/collision.h"
#include "scene/faction.h"

ecs_comp_define(SceneFactionComp);
ecs_comp_define(SceneFactionStatsComp);

static void ecs_combine_faction_stats(void* dataA, void* dataB) {
  SceneFactionStatsComp* compA = dataA;
  SceneFactionStatsComp* compB = dataB;

  for (SceneFaction faction = 0; faction != SceneFaction_Count; ++faction) {
    for (SceneFactionStat stat = 0; stat != SceneFactionStat_Count; ++stat) {
      compA->values[faction][stat] += compB->values[faction][stat];
    }
  }
}

ecs_module_init(scene_faction_module) {
  ecs_register_comp(SceneFactionComp);
  ecs_register_comp(SceneFactionStatsComp, .combinator = ecs_combine_faction_stats);
}

String scene_faction_name(const SceneFaction faction) {
  diag_assert(faction < SceneFaction_Count);
  static const String g_names[] = {
      string_static("A"),
      string_static("B"),
      string_static("C"),
      string_static("D"),
  };
  ASSERT(array_elems(g_names) == SceneFaction_Count, "Incorrect number of faction names");
  return g_names[faction];
}

SceneLayer scene_faction_layers(const SceneFaction faction) {
  switch (faction) {
  case SceneFaction_A:
    return SceneLayer_UnitFactionA;
  case SceneFaction_B:
    return SceneLayer_UnitFactionB;
  case SceneFaction_C:
    return SceneLayer_UnitFactionC;
  case SceneFaction_D:
    return SceneLayer_UnitFactionD;
  case SceneFaction_None:
    return SceneLayer_UnitFactionNone;
  case SceneFaction_Count:
    break;
  }
  diag_crash_msg("Unsupported faction");
}

bool scene_is_friendly(const SceneFactionComp* a, const SceneFactionComp* b) {
  if (a == null || b == null) {
    return false;
  }
  if (a->id == SceneFaction_None || b->id == SceneFaction_None) {
    return false;
  }
  return a->id == b->id;
}

bool scene_is_hostile(const SceneFactionComp* a, const SceneFactionComp* b) {
  return !scene_is_friendly(a, b);
}

SceneFactionStatsComp* scene_faction_stats_init(EcsWorld* world) {
  return ecs_world_add_t(world, ecs_world_global(world), SceneFactionStatsComp);
}

void scene_faction_stats_clear(SceneFactionStatsComp* comp) { mem_set(array_mem(comp->values), 0); }
