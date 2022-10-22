#pragma once
#include "core_dynarray.h"
#include "ecs_def.h"

#include "module_internal.h"

typedef struct {
  String            name;
  usize             size, align;
  EcsCompDestructor destructor;
  i32               destructOrder;
  EcsCompCombinator combinator;
} EcsCompDef;

typedef struct {
  String      name;
  EcsViewInit initRoutine;
} EcsViewDef;

typedef struct {
  String           name;
  EcsSystemRoutine routine;
  EcsSystemFlags   flags;
  i32              order;
  u16              parallelCount;
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

EcsCompId   ecs_def_register_comp(EcsDef*, const EcsCompConfig*);
EcsViewId   ecs_def_register_view(EcsDef*, const EcsViewConfig*);
EcsSystemId ecs_def_register_system(EcsDef*, const EcsSystemConfig*);
void        ecs_def_update_order(EcsDef*, EcsSystemId, i32 order);
void        ecs_def_update_parallel(EcsDef*, EcsSystemId, u16 parallelCount);

EcsCompDestructor ecs_def_comp_destructor(const EcsDef*, EcsCompId);
i32               ecs_def_comp_destruct_order(const EcsDef*, EcsCompId);
EcsCompCombinator ecs_def_comp_combinator(const EcsDef*, EcsCompId);

/**
 * Disallow any further modifications to this definition.
 */
void ecs_def_freeze(EcsDef*);

/**
 * Re-allow further modifications to this definition.
 */
void ecs_def_unfreeze(EcsDef*);
