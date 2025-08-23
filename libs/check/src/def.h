#pragma once
#include "check/def.h"
#include "core/dynarray.h"
#include "core/string.h"

typedef struct {
  String           name;
  CheckSpecRoutine routine;
} CheckSpecDef;

struct sCheckDef {
  DynArray   specs; // CheckSpecDef[]
  Allocator* alloc;
};
