#pragma once
#include "check_def.h"
#include "core_dynarray.h"
#include "core_string.h"

typedef struct {
  String           name;
  CheckSpecRoutine routine;
} CheckSpecDef;

struct sCheckDef {
  DynArray   specs; // CheckSpecDef[]
  Allocator* alloc;
};
