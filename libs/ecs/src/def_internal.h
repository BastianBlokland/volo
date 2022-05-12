#pragma once
#include "core_dynarray.h"
#include "ecs_def.h"

#include "module_internal.h"

#define ecs_comp_max_size 1024

/**
 * Compute the required size for a component mask.
 * NOTE: Rounded up to dword (64 bit value) size.
 */
#define ecs_def_mask_size(_DEF_) ((bits_to_dwords((_DEF_)->components.size) + 1) * sizeof(u64))

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

EcsCompDestructor ecs_def_comp_destructor(const EcsDef*, EcsCompId);
i32               ecs_def_comp_destruct_order(const EcsDef*, EcsCompId);
EcsCompCombinator ecs_def_comp_combinator(const EcsDef*, EcsCompId);

/**
 * Dissallow any further modications to this definition.
 */
void ecs_def_freeze(EcsDef*);

/**
 * Reallow further modications to this definition.
 */
void ecs_def_unfreeze(EcsDef*);
