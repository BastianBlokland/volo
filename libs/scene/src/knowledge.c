#include "core_alloc.h"
#include "ecs_world.h"
#include "script_mem.h"

ecs_comp_define(SceneKnowledgeComp) { ScriptMem* memory; };

static void ecs_destruct_knowledge_comp(void* data) {
  SceneKnowledgeComp* brain = data;
  script_mem_destroy(brain->memory);
}

ecs_module_init(scene_knowledge_module) {
  ecs_register_comp(SceneKnowledgeComp, .destructor = ecs_destruct_knowledge_comp);
}

ScriptVal scene_knowledge_get(const SceneKnowledgeComp* brain, const StringHash key) {
  return script_mem_get(brain->memory, key);
}

void scene_knowledge_set(SceneKnowledgeComp* brain, const StringHash key, const ScriptVal value) {
  script_mem_set(brain->memory, key, value);
}

void scene_knowledge_set_null(SceneKnowledgeComp* brain, const StringHash key) {
  script_mem_set_null(brain->memory, key);
}

const ScriptMem* scene_knowledge_memory(const SceneKnowledgeComp* brain) { return brain->memory; }

ScriptMem* scene_knowledge_memory_mut(SceneKnowledgeComp* brain) { return brain->memory; }

SceneKnowledgeComp* scene_knowledge_add(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, SceneKnowledgeComp, .memory = script_mem_create(g_alloc_heap));
}
