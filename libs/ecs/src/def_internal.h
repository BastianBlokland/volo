#pragma once
#include "core_dynarray.h"
#include "ecs_def.h"

#include "module_internal.h"

typedef struct {
  String name;
  usize  size;
  usize  align;
} EcsCompDef;

typedef struct {
  String      name;
  EcsViewInit initRoutine;
} EcsViewDef;

struct sEcsDef {
  DynArray   modules;    // EcsModuleDef[]
  DynArray   components; // EcsCompDef[]
  DynArray   views;      // EcsViewDef[]
  Allocator* alloc;
};

/**
 * Look-up a module definition by name.
 * Note: returns 'null' when no module was found with the given name.
 */
const EcsModuleDef* ecs_def_module_by_name(const EcsDef*, String name);

/**
 * Look-up a component definition by name.
 * Note: returns 'null' when no component was found with the given name.
 */
const EcsCompDef* ecs_def_comp_by_name(const EcsDef*, String name);

/**
 * Register a new component definition.
 */
EcsCompId ecs_def_register_comp(EcsDef*, String name, usize size, usize align);

/**
 * Register a new view definition.
 */
EcsViewId ecs_def_register_view(EcsDef*, String name, EcsViewInit);
