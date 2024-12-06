#include "core_alloc.h"
#include "ecs_module.h"
#include "ecs_world.h"
#include "script_mem.h"

ecs_comp_define(SceneKnowledgeComp) { ScriptMem memory; };

static void ecs_destruct_knowledge_comp(void* data) {
  SceneKnowledgeComp* k = data;
  script_mem_destroy(&k->memory);
}

static void ecs_combine_knowledge_comp(void* dataA, void* dataB) {
  SceneKnowledgeComp* compA = dataA;
  SceneKnowledgeComp* compB = dataB;

  ScriptMem* memA = &compA->memory;
  ScriptMem* memB = &compB->memory;
  for (ScriptMemItr itr = script_mem_begin(memB); itr.key; itr = script_mem_next(memB, itr)) {
    script_mem_store(memA, itr.key, script_mem_load(memB, itr.key));
  }

  script_mem_destroy(&compB->memory);
}

ecs_module_init(scene_knowledge_module) {
  ecs_register_comp(
      SceneKnowledgeComp,
      .destructor = ecs_destruct_knowledge_comp,
      .combinator = ecs_combine_knowledge_comp);
}

ScriptVal scene_knowledge_load(const SceneKnowledgeComp* k, const StringHash key) {
  return script_mem_load(&k->memory, key);
}

void scene_knowledge_store(SceneKnowledgeComp* k, const StringHash key, const ScriptVal value) {
  script_mem_store(&k->memory, key, value);
}

const ScriptMem* scene_knowledge_memory(const SceneKnowledgeComp* k) { return &k->memory; }

ScriptMem* scene_knowledge_memory_mut(SceneKnowledgeComp* k) { return &k->memory; }

SceneKnowledgeComp* scene_knowledge_add(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(world, entity, SceneKnowledgeComp, .memory = script_mem_create());
}
