#pragma once
#include "core_memory.h"

typedef struct sAllocator Allocator;

extern Allocator g_allocatorHeap;

Mem  alloc_alloc(Allocator*, usize);
void alloc_free(Allocator*, Mem);
