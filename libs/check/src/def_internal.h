#pragma once
#include "core_dynarray.h"
#include "core_string.h"

#include "check_def.h"

typedef struct {
  String           name;
  CheckSpecRoutine routine;
} CheckSpecDef;

struct sCheckDef {
  DynArray   specs; // CheckSpecDef[]
  Allocator* alloc;
};
