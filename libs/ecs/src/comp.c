#include "core_compare.h"
#include "ecs_comp.h"

i8 ecs_compare_comp(const void* a, const void* b) { return compare_u16(a, b); }
