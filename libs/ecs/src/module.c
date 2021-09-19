#include "def_internal.h"

struct sEcsModuleBuilder {
  EcsDef*       def;
  EcsModuleDef* module;
};

EcsModuleDef ecs_module_create(EcsDef* def, const String name, const EcsModuleInit initRoutine) {
  EcsModuleDef module = {
      .name         = string_dup(def->alloc, name),
      .componentIds = dynarray_create_t(def->alloc, EcsCompId, 8),
  };
  EcsModuleBuilder builder = {.def = def, .module = &module};
  initRoutine(&builder);
  return module;
}

void ecs_module_destroy(EcsDef* def, EcsModuleDef* module) {
  string_free(def->alloc, module->name);
  dynarray_destroy(&module->componentIds);
}

EcsCompId ecs_module_register_comp_id(
    EcsModuleBuilder* builder, const String name, const usize size, const usize align) {

  const EcsCompId id = ecs_def_register_comp(builder->def, name, size, align);
  *dynarray_push_t(&builder->module->componentIds, EcsCompId) = id;
  return id;
}
