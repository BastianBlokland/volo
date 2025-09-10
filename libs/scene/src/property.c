#include "ecs/module.h"
#include "ecs/world.h"
#include "scene/property.h"
#include "script/mem.h"
#include "script/val.h"

ecs_comp_define(ScenePropertyComp) { ScriptMem memory; };

static void ecs_destruct_prop_comp(void* data) {
  ScenePropertyComp* comp = data;
  script_mem_destroy(&comp->memory);
}

static void ecs_combine_prop_comp(void* dataA, void* dataB) {
  ScenePropertyComp* compA = dataA;
  ScenePropertyComp* compB = dataB;

  ScriptMem* memA = &compA->memory;
  ScriptMem* memB = &compB->memory;
  for (ScriptMemItr itr = script_mem_begin(memB); itr.key; itr = script_mem_next(memB, itr)) {
    script_mem_store(memA, itr.key, script_mem_load(memB, itr.key));
  }

  script_mem_destroy(&compB->memory);
}

ecs_system_define(ScenePropertyInitSys) {
  const EcsEntityId globalEntity = ecs_world_global(world);
  if (!ecs_world_has_t(world, globalEntity, ScenePropertyComp)) {
    scene_prop_add(world, globalEntity);
  }
}

ecs_module_init(scene_property_module) {
  ecs_register_comp(
      ScenePropertyComp, .destructor = ecs_destruct_prop_comp, .combinator = ecs_combine_prop_comp);

  ecs_register_system(ScenePropertyInitSys);
}

ScriptVal scene_prop_load(const ScenePropertyComp* k, const StringHash key) {
  return script_mem_load(&k->memory, key);
}

void scene_prop_store(ScenePropertyComp* k, const StringHash key, const ScriptVal value) {
  script_mem_store(&k->memory, key, value);
}

const ScriptMem* scene_prop_memory(const ScenePropertyComp* k) { return &k->memory; }

ScriptMem* scene_prop_memory_mut(ScenePropertyComp* k) { return &k->memory; }

ScenePropertyComp* scene_prop_add(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(world, entity, ScenePropertyComp, .memory = script_mem_create());
}
