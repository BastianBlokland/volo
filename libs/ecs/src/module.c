#include "core_diag.h"

#include "def_internal.h"

struct sEcsModuleBuilder {
  EcsDef*       def;
  EcsModuleDef* module;
};

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

EcsCompId ecs_module_register_comp(
    EcsModuleBuilder* builder, const String name, const usize size, const usize align) {

  const EcsCompId id = ecs_def_register_comp(builder->def, name, size, align);
  *dynarray_push_t(&builder->module->componentIds, EcsCompId) = id;
  return id;
}

EcsViewId ecs_module_register_view(
    EcsModuleBuilder* builder, const String name, const EcsViewInit initRoutine) {

  const EcsViewId id = ecs_def_register_view(builder->def, name, initRoutine);
  *dynarray_push_t(&builder->module->viewIds, EcsViewId) = id;
  return id;
}

void ecs_module_view_with(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'with' filter as component '{}' is allready marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->filterWith, comp);
}

void ecs_module_view_without(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWith, comp),
      "Unable to apply 'without' filter as component '{}' is allready marked as 'with'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));
  diag_assert_msg(
      !bitset_test(builder->accessRead, comp),
      "Unable to apply 'without' filter as component '{}' is allready marked with 'read' access",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->filterWithout, comp);
}

void ecs_module_view_read(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'read' access as component '{}' is allready marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->filterWith, comp);
  bitset_set(builder->accessRead, comp);
}

void ecs_module_view_write(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'write' access as component '{}' is allready marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->filterWith, comp);
  bitset_set(builder->accessRead, comp);
  bitset_set(builder->accessWrite, comp);
}

void ecs_module_view_maybe_read(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'maybe-read' access as component '{}' is allready marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->accessRead, comp);
}

void ecs_module_view_maybe_write(EcsViewBuilder* builder, const EcsCompId comp) {
  diag_assert_msg(
      !bitset_test(builder->filterWithout, comp),
      "Unable to apply 'maybe-write' access as component '{}' is allready marked as 'without'",
      fmt_text(ecs_def_comp_name(builder->def, comp)));

  bitset_set(builder->accessRead, comp);
  bitset_set(builder->accessWrite, comp);
}

EcsSystemId ecs_module_register_system(
    EcsModuleBuilder*      builder,
    const String           name,
    const EcsSystemRoutine routine,
    const EcsViewId*       views,
    const usize            viewCount) {

  const EcsSystemId id = ecs_def_register_system(builder->def, name, routine, views, viewCount);
  *dynarray_push_t(&builder->module->systemIds, EcsSystemId) = id;
  return id;
}
