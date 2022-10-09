#include "ecs_world.h"
#include "scene_faction.h"

ecs_comp_define_public(SceneFactionComp);

ecs_module_init(scene_faction_module) { ecs_register_comp(SceneFactionComp); }

bool scene_is_friendly(const SceneFactionComp* a, const SceneFactionComp* b) {
  if (!a || !b) {
    return false;
  }
  return a->id == b->id;
}

bool scene_is_hostile(const SceneFactionComp* a, const SceneFactionComp* b) {
  return !scene_is_friendly(a, b);
}
