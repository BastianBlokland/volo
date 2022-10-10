#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"
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

bool scene_is_friendly(const SceneFactionComp* a, const SceneFactionComp* b) {
  if (!a || !b) {
    return false;
  }
  return a->id == b->id;
}

bool scene_is_hostile(const SceneFactionComp* a, const SceneFactionComp* b) {
  return !scene_is_friendly(a, b);
}
