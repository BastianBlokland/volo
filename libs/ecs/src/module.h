#pragma once
#include "core/bitset.h"
#include "core/dynarray.h"
#include "ecs/def.h"
#include "ecs/module.h"

typedef struct {
  String   name;
  DynArray componentIds; // EcsCompId[]
  DynArray viewIds;      // EcsViewId[]
  DynArray systemIds;    // EcsSystemId[]
} EcsModuleDef;

struct sEcsViewBuilder {
  const EcsDef* def;
  EcsViewFlags  flags;
  BitSet        filterWith, filterWithout;
  BitSet        accessRead, accessWrite;
};

EcsModuleDef ecs_module_create(EcsDef*, EcsModuleId, String name, EcsModuleInit, const void* ctx);
void         ecs_module_destroy(EcsDef*, EcsModuleDef*);
