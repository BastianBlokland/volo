#pragma once
#include "core_dynarray.h"
#include "ecs_def.h"

#include "module_internal.h"

#define ecs_comp_max_size 1024

typedef struct {
  String name;
  usize  size;
  usize  align;
} EcsCompDef;

typedef struct {
  String      name;
  EcsViewInit initRoutine;
} EcsViewDef;

typedef struct {
  String           name;
  EcsSystemRoutine routine;
  DynArray         viewIds; // EcsViewId[] (NOTE: kept sorted)
} EcsSystemDef;

typedef enum {
  EcsDefFlags_None,
  EcsDefFlags_Frozen = 1 << 0,
} EcsDefFlags;

struct sEcsDef {
  DynArray    modules;    // EcsModuleDef[]
  DynArray    components; // EcsCompDef[]
  DynArray    views;      // EcsViewDef[]
  DynArray    systems;    // EcsSystemDef[]
  EcsDefFlags flags;
  Allocator*  alloc;
};

EcsCompId   ecs_def_register_comp(EcsDef*, String name, usize size, usize align);
EcsViewId   ecs_def_register_view(EcsDef*, String name, EcsViewInit);
EcsSystemId ecs_def_register_system(
    EcsDef*, String name, EcsSystemRoutine, const EcsViewId* views, usize viewCount);

/**
 * Dissallow any further modications to this definition.
 */
void ecs_def_freeze(EcsDef*);

/**
 * Reallow further modications to this definition.
 */
void ecs_def_unfreeze(EcsDef*);
