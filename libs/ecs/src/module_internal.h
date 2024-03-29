#pragma once
#include "core_bitset.h"
#include "core_dynarray.h"
#include "ecs_def.h"
#include "ecs_module.h"

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

EcsModuleDef ecs_module_create(EcsDef*, EcsModuleId, String name, EcsModuleInit);
void         ecs_module_destroy(EcsDef*, EcsModuleDef*);
