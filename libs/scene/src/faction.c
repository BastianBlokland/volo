#include "core_array.h"
#include "core_diag.h"
#include "scene_collision.h"
#include "scene_faction.h"

ecs_comp_define_public(SceneFactionComp);

ecs_module_init(scene_faction_module) { ecs_register_comp(SceneFactionComp); }

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
    return SceneLayer_None;
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
