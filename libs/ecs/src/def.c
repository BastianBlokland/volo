#include "core_alloc.h"
#include "core_diag.h"

#include "def_internal.h"
#include "module_internal.h"

static const EcsCompDef* ecs_def_comp(const EcsDef* def, const EcsCompId id) {
  diag_assert_msg(id < def->components.size, "Invalid component id '{}'", fmt_int(id));
  return dynarray_at_t(&def->components, (usize)id, EcsCompDef);
}

static const EcsViewDef* ecs_def_view(const EcsDef* def, const EcsViewId id) {
  diag_assert_msg(id < def->views.size, "Invalid view id '{}'", fmt_int(id));
  return dynarray_at_t(&def->views, (usize)id, EcsViewDef);
}

EcsDef* ecs_def_create(Allocator* alloc) {
  EcsDef* def = alloc_alloc_t(alloc, EcsDef);
  *def        = (EcsDef){
      .modules    = dynarray_create_t(alloc, EcsModuleDef, 64),
      .components = dynarray_create_t(alloc, EcsCompDef, 128),
      .views      = dynarray_create_t(alloc, EcsViewDef, 128),
      .alloc      = alloc,
  };
  return def;
}

void ecs_def_destroy(EcsDef* def) {

  dynarray_for_t(&def->modules, EcsModuleDef, module, { ecs_module_destroy(def, module); });
  dynarray_destroy(&def->modules);

  dynarray_for_t(&def->components, EcsCompDef, comp, { string_free(def->alloc, comp->name); });
  dynarray_destroy(&def->components);

  dynarray_for_t(&def->views, EcsViewDef, view, { string_free(def->alloc, view->name); });
  dynarray_destroy(&def->views);

  alloc_free_t(def->alloc, def);
}

void ecs_def_register_module(EcsDef* def, const String name, const EcsModuleInit initRoutine) {
  diag_assert_msg(!ecs_def_module_by_name(def, name), "Duplicate module name '{}'", fmt_text(name));
  *dynarray_push_t(&def->modules, EcsModuleDef) = ecs_module_create(def, name, initRoutine);
}

String ecs_def_comp_name(const EcsDef* def, const EcsCompId id) {
  return ecs_def_comp(def, id)->name;
}

usize ecs_def_comp_size(const EcsDef* def, const EcsCompId id) {
  return ecs_def_comp(def, id)->size;
}

usize ecs_def_comp_align(const EcsDef* def, const EcsCompId id) {
  return ecs_def_comp(def, id)->align;
}

String ecs_def_view_name(const EcsDef* def, const EcsViewId id) {
  return ecs_def_view(def, id)->name;
}

const EcsModuleDef* ecs_def_module_by_name(const EcsDef* def, const String name) {
  dynarray_for_t((DynArray*)&def->modules, EcsModuleDef, module, {
    if (string_eq(module->name, name)) {
      return module;
    }
  });
  return null;
}

const EcsCompDef* ecs_def_comp_by_name(const EcsDef* def, const String name) {
  dynarray_for_t((DynArray*)&def->components, EcsCompDef, comp, {
    if (string_eq(comp->name, name)) {
      return comp;
    }
  });
  return null;
}

EcsCompId
ecs_def_register_comp(EcsDef* def, const String name, const usize size, const usize align) {
  diag_assert_msg(
      !ecs_def_comp_by_name(def, name), "Duplicate component name '{}'", fmt_text(name));
  diag_assert_msg(
      bits_ispow2(align), "Component alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      (size & (align - 1)) == 0,
      "Component size '{}' is not a multiple of the alignment '{}'",
      fmt_size(size),
      fmt_int(align));

  EcsCompId id                                   = (EcsCompId)def->components.size;
  *dynarray_push_t(&def->components, EcsCompDef) = (EcsCompDef){
      .name  = string_dup(def->alloc, name),
      .size  = size,
      .align = align,
  };
  return id;
}

EcsViewId ecs_def_register_view(EcsDef* def, const String name, const EcsViewInit initRoutine) {
  EcsViewId id                              = (EcsViewId)def->views.size;
  *dynarray_push_t(&def->views, EcsViewDef) = (EcsViewDef){
      .name        = string_dup(def->alloc, name),
      .initRoutine = initRoutine,
  };
  return id;
}
