#include "ecs_world.h"
#include "scene_unit.h"

ecs_comp_define_public(SceneUnitComp);
ecs_comp_define_public(SceneUnitInfantryComp);
ecs_comp_define_public(SceneUnitStructureComp);

ecs_module_init(scene_unit_module) {
  ecs_register_comp_empty(SceneUnitComp);
  ecs_register_comp_empty(SceneUnitInfantryComp);
  ecs_register_comp_empty(SceneUnitStructureComp);
}
