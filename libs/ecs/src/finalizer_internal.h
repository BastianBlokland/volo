#pragma once
#include "core_dynarray.h"
#include "ecs_def.h"

/**
 * Finalizer is responsible for invoking component destructors in the specified destruction order.
 * Many components to finalize can be queued up using 'push' and are executed when the finalizer is
 * flushed.
 */

typedef struct {
  i32               destructOrder;
  EcsCompDestructor destructor;
  void*             compData;
} EcsFinalizerEntry;

typedef struct {
  const EcsDef* def;
  DynArray      entries; // EcsFinalizerEntry[]
} EcsFinalizer;

EcsFinalizer ecs_finalizer_create(Allocator*, const EcsDef* def);
void         ecs_finalizer_destroy(EcsFinalizer*);
void         ecs_finalizer_push(EcsFinalizer*, EcsCompId, void* compData);
void         ecs_finalizer_flush(EcsFinalizer*);
