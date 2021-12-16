#include "core_diag.h"

#include "def_internal.h"

struct sEcsModuleBuilder {
  EcsDef*       def;
  EcsModuleDef* module;
};

i8 ecs_compare_view(const void* a, const void* b) { return compare_u16(a, b); }
i8 ecs_compare_system(const void* a, const void* b) { return compare_u16(a, b); }

EcsModuleDef ecs_module_create(EcsDef* def, const String name, const EcsModuleInit initRoutine) {
  EcsModuleDef module = {
      .name         = string_dup(def->alloc, name),
      .componentIds = dynarray_create_t(def->alloc, EcsCompId, 8),
      .viewIds      = dynarray_create_t(def->alloc, EcsViewId, 8),
      .systemIds    = dynarray_create_t(def->alloc, EcsSystemId, 8),
  };
  EcsModuleBuilder builder = {.def = def, .module = &module};
  initRoutine(&builder);
  return module;
}

void ecs_module_destroy(EcsDef* def, EcsModuleDef* module) {
  string_free(def->alloc, module->name);
  dynarray_destroy(&module->componentIds);
  dynarray_destroy(&module->viewIds);
  dynarray_destroy(&module->systemIds);
}

EcsCompId
ecs_module_register_comp(EcsModuleBuilder* builder, EcsCompId* var, const EcsCompConfig* config) {
  const EcsCompId id = ecs_def_register_comp(builder->def, config);

  *dynarray_push_t(&builder->module->componentIds, EcsCompId) = id;

  if (var) {
    *var = id;
  }
  return id;
}

EcsViewId
ecs_module_register_view(EcsModuleBuilder* builder, EcsViewId* var, const EcsViewConfig* config) {
  const EcsViewId id = ecs_def_register_view(builder->def, config);
  *dynarray_push_t(&builder->module->viewIds, EcsViewId) = id;

  if (var) {
    *var = id;
  }
  return id;
}

void ecs_module_access_with(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'with' acesss as component '{}' is already marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->filterWith, comp);
}

void ecs_module_access_without(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWith, comp),
      "Unable to apply 'without' acesss as component '{}' is already marked as 'with'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));
  diag_assert_msg(
      !bitset_test(builder->accessRead, comp),
      "Unable to apply 'without' acesss as component '{}' is already marked with 'read' access",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->filterWithout, comp);
}

void ecs_module_access_read(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'read' access as component '{}' is already marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->filterWith, comp);
  bitset_set(builder->accessRead, comp);
}

void ecs_module_access_write(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'write' access as component '{}' is already marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->filterWith, comp);
  bitset_set(builder->accessRead, comp);
  bitset_set(builder->accessWrite, comp);
}

void ecs_module_access_maybe_read(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'maybe-read' access as component '{}' is already marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->accessRead, comp);
}

void ecs_module_access_maybe_write(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'maybe-write' access as component '{}' is already marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->accessRead, comp);
  bitset_set(builder->accessWrite, comp);
}

EcsSystemId ecs_module_register_system(
    EcsModuleBuilder* builder, EcsSystemId* var, const EcsSystemConfig* config) {

  const EcsSystemId id = ecs_def_register_system(builder->def, config);
  *dynarray_push_t(&builder->module->systemIds, EcsSystemId) = id;

  if (var) {
    *var = id;
  }
  return id;
}
