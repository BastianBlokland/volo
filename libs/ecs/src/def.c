#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"

#include "comp_internal.h"
#include "def_internal.h"
#include "module_internal.h"

static const EcsModuleDef* ecs_def_module(const EcsDef* def, const EcsModuleId id) {
  diag_assert_msg(id < def->modules.size, "Invalid module id '{}'", fmt_int(id));
  return dynarray_begin_t(&def->modules, EcsModuleDef) + id;
}

INLINE_HINT static const EcsCompDef* ecs_def_comp(const EcsDef* def, const EcsCompId id) {
  diag_assert_msg(id < def->components.size, "Invalid component id '{}'", fmt_int(id));
  return dynarray_begin_t(&def->components, EcsCompDef) + id;
}

static const EcsViewDef* ecs_def_view(const EcsDef* def, const EcsViewId id) {
  diag_assert_msg(id < def->views.size, "Invalid view id '{}'", fmt_int(id));
  return dynarray_begin_t(&def->views, EcsViewDef) + id;
}

static const EcsSystemDef* ecs_def_system(const EcsDef* def, const EcsSystemId id) {
  diag_assert_msg(id < def->systems.size, "Invalid system id '{}'", fmt_int(id));
  return dynarray_begin_t(&def->systems, EcsSystemDef) + id;
}

static EcsSystemDef* ecs_def_system_mutable(EcsDef* def, const EcsSystemId id) {
  diag_assert_msg(id < def->systems.size, "Invalid system id '{}'", fmt_int(id));
  return dynarray_begin_t(&def->systems, EcsSystemDef) + id;
}

MAYBE_UNUSED static const EcsModuleDef*
ecs_def_module_by_name(const EcsDef* def, const String name) {
  dynarray_for_t(&def->modules, EcsModuleDef, module) {
    if (string_eq(module->name, name)) {
      return module;
    }
  }
  return null;
}

MAYBE_UNUSED static const EcsCompDef* ecs_def_comp_by_name(const EcsDef* def, const String name) {
  dynarray_for_t(&def->components, EcsCompDef, comp) {
    if (string_eq(comp->name, name)) {
      return comp;
    }
  }
  return null;
}

EcsDef* ecs_def_create(Allocator* alloc) {
  EcsDef* def = alloc_alloc_t(alloc, EcsDef);
  *def        = (EcsDef){
      .modules    = dynarray_create_t(alloc, EcsModuleDef, 64),
      .components = dynarray_create_t(alloc, EcsCompDef, 128),
      .views      = dynarray_create_t(alloc, EcsViewDef, 128),
      .systems    = dynarray_create_t(alloc, EcsSystemDef, 128),
      .alloc      = alloc,
  };
  return def;
}

void ecs_def_destroy(EcsDef* def) {
  diag_assert_msg(!(def->flags & EcsDefFlags_Frozen), "Unable to destroy a frozen definition");

  dynarray_for_t(&def->modules, EcsModuleDef, module) { ecs_module_destroy(def, module); }
  dynarray_for_t(&def->systems, EcsSystemDef, system) { dynarray_destroy(&system->viewIds); }

  dynarray_destroy(&def->modules);
  dynarray_destroy(&def->components);
  dynarray_destroy(&def->views);
  dynarray_destroy(&def->systems);

  alloc_free_t(def->alloc, def);
}

EcsModuleId
ecs_def_register_module(EcsDef* def, const String name, const EcsModuleInit initRoutine) {
  diag_assert_msg(!(def->flags & EcsDefFlags_Frozen), "Unable to modify a frozen definition");
  diag_assert_msg(!ecs_def_module_by_name(def, name), "Duplicate module name '{}'", fmt_text(name));

  const EcsModuleId id                          = (EcsModuleId)def->modules.size;
  *dynarray_push_t(&def->modules, EcsModuleDef) = ecs_module_create(def, id, name, initRoutine);
  return id;
}

String ecs_def_module_name(const EcsDef* def, const EcsModuleId id) {
  return ecs_def_module(def, id)->name;
}

u32 ecs_def_comp_count(const EcsDef* def) { return (u32)def->components.size; }
u32 ecs_def_view_count(const EcsDef* def) { return (u32)def->views.size; }
u32 ecs_def_system_count(const EcsDef* def) { return (u32)def->systems.size; }
u32 ecs_def_module_count(const EcsDef* def) { return (u32)def->modules.size; }

EcsModuleId ecs_def_comp_module(const EcsDef* def, const EcsCompId id) {
  return ecs_def_comp(def, id)->moduleId;
}

EcsModuleId ecs_def_view_module(const EcsDef* def, const EcsViewId id) {
  return ecs_def_view(def, id)->moduleId;
}

EcsModuleId ecs_def_system_module(const EcsDef* def, const EcsSystemId id) {
  return ecs_def_system(def, id)->moduleId;
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

String ecs_def_system_name(const EcsDef* def, const EcsSystemId id) {
  return ecs_def_system(def, id)->name;
}

i32 ecs_def_system_order(const EcsDef* def, const EcsSystemId id) {
  return ecs_def_system(def, id)->order;
}

u32 ecs_def_system_parallel(const EcsDef* def, const EcsSystemId id) {
  return ecs_def_system(def, id)->parallelCount;
}

EcsSystemFlags ecs_def_system_flags(const EcsDef* def, const EcsSystemId id) {
  return ecs_def_system(def, id)->flags;
}

EcsDefSystemViews ecs_def_system_views(const EcsDef* def, const EcsSystemId id) {
  const EcsSystemDef* sysDef = ecs_def_system(def, id);
  return (EcsDefSystemViews){
      .values = sysDef->viewIds.data.ptr,
      .count  = sysDef->viewIds.size,
  };
}

bool ecs_def_system_has_access(const EcsDef* def, const EcsSystemId sysId, const EcsViewId id) {
  const EcsSystemDef* system = ecs_def_system(def, sysId);
  return dynarray_search_binary((DynArray*)&system->viewIds, ecs_compare_view, &id) != null;
}

EcsCompId ecs_def_register_comp(EcsDef* def, const EcsModuleId modId, const EcsCompConfig* cfg) {

  diag_assert_msg(!(def->flags & EcsDefFlags_Frozen), "Unable to modify a frozen definition");
  diag_assert_msg(
      !ecs_def_comp_by_name(def, cfg->name), "Duplicate component name '{}'", fmt_text(cfg->name));
  diag_assert_msg(
      bits_ispow2(cfg->align),
      "Component alignment '{}' is not a power-of-two",
      fmt_int(cfg->align));
  diag_assert_msg(
      bits_aligned(cfg->size, cfg->align),
      "Component size '{}' is not a multiple of the alignment '{}'",
      fmt_size(cfg->size),
      fmt_int(cfg->align));
  diag_assert_msg(
      cfg->size <= ecs_comp_max_size,
      "Component size '{}' is bigger then the maximum of '{}'",
      fmt_size(cfg->size),
      fmt_size(ecs_comp_max_size));
  diag_assert_msg(!cfg->destructor || cfg->size > 0, "Empty components do not support destructors");
  diag_assert_msg(!cfg->combinator || cfg->size > 0, "Empty components do not support combinators");

  EcsCompId id                                   = (EcsCompId)def->components.size;
  *dynarray_push_t(&def->components, EcsCompDef) = (EcsCompDef){
      .moduleId      = modId,
      .name          = cfg->name, // Name is always persistently allocated, no need to copy.
      .size          = cfg->size,
      .align         = cfg->align,
      .destructor    = cfg->destructor,
      .destructOrder = cfg->destructOrder,
      .combinator    = cfg->combinator,
  };
  return id;
}

EcsViewId ecs_def_register_view(EcsDef* def, const EcsModuleId modId, const EcsViewConfig* cfg) {
  diag_assert_msg(!(def->flags & EcsDefFlags_Frozen), "Unable to modify a frozen definition");

  const EcsViewId id                        = (EcsViewId)def->views.size;
  *dynarray_push_t(&def->views, EcsViewDef) = (EcsViewDef){
      .moduleId    = modId,
      .name        = cfg->name, // Name is always persistently allocated, no need to copy.
      .initRoutine = cfg->initRoutine,
  };
  return id;
}

EcsSystemId
ecs_def_register_system(EcsDef* def, const EcsModuleId modId, const EcsSystemConfig* cfg) {
  diag_assert_msg(!(def->flags & EcsDefFlags_Frozen), "Unable to modify a frozen definition");

  const EcsSystemId id        = (EcsSystemId)def->systems.size;
  EcsSystemDef*     systemDef = dynarray_push_t(&def->systems, EcsSystemDef);

  *systemDef = (EcsSystemDef){
      .moduleId      = modId,
      .name          = cfg->name, // Name is always persistently allocated, no need to copy.
      .routine       = cfg->routine,
      .flags         = cfg->flags,
      .parallelCount = 1,
      .viewIds       = dynarray_create_t(def->alloc, EcsViewId, cfg->viewCount),
  };

  for (usize i = 0; i != cfg->viewCount; ++i) {
    const EcsViewId* idPtr = &cfg->views[i];
    *dynarray_insert_sorted_t(&systemDef->viewIds, EcsViewId, ecs_compare_view, idPtr) = *idPtr;
  }

  return id;
}

void ecs_def_update_order(EcsDef* def, const EcsSystemId system, const i32 order) {
  ecs_def_system_mutable(def, system)->order = order;
}

void ecs_def_update_parallel(EcsDef* def, const EcsSystemId system, const u16 parallelCount) {
  diag_assert_msg(parallelCount, "Parallel count of 0 is not supported");
  ecs_def_system_mutable(def, system)->parallelCount = parallelCount;
}

EcsCompDestructor ecs_def_comp_destructor(const EcsDef* def, const EcsCompId id) {
  return ecs_def_comp(def, id)->destructor;
}

i32 ecs_def_comp_destruct_order(const EcsDef* def, const EcsCompId id) {
  return ecs_def_comp(def, id)->destructOrder;
}

EcsCompCombinator ecs_def_comp_combinator(const EcsDef* def, const EcsCompId id) {
  return ecs_def_comp(def, id)->combinator;
}

void ecs_def_freeze(EcsDef* def) { def->flags |= EcsDefFlags_Frozen; }

void ecs_def_unfreeze(EcsDef* def) { def->flags &= ~EcsDefFlags_Frozen; }
