#pragma once
#include "core_dynarray.h"
#include "ecs_def.h"

typedef struct {
  String   name;
  DynArray componentIds; // EcsCompId[]
} EcsModuleDef;

EcsModuleDef ecs_module_create(EcsDef*, String name, EcsModuleInit);
void         ecs_module_destroy(EcsDef*, EcsModuleDef*);
