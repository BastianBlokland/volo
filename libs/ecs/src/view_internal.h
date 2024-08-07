#include "core_bitset.h"
#include "core_dynarray.h"
#include "ecs_view.h"

#include "def_internal.h"
#include "storage_internal.h"

typedef enum {
  EcsViewMask_FilterWith,
  EcsViewMask_FilterWithout,
  EcsViewMask_AccessRead,
  EcsViewMask_AccessWrite,
} EcsViewMaskType;

struct sEcsView {
  const EcsDef*     def;
  const EcsViewDef* viewDef;
  EcsViewFlags      flags;
  u16               compCount;
  EcsStorage*       storage;
  Mem               masks;
  DynArray          archetypes; // EcsArchetypeId[] (NOTE: kept sorted)
#ifndef VOLO_FAST
  DynArray exclusiveEntities; // EcsEntityId[] (NOTE: kept sorted).
#endif
};

EcsView ecs_view_create(Allocator*, EcsStorage*, const EcsDef*, const EcsViewDef*);
void    ecs_view_destroy(Allocator*, const EcsDef*, EcsView*);
BitSet  ecs_view_mask(const EcsView*, EcsViewMaskType);
bool    ecs_view_conflict(const EcsView* a, const EcsView* b);
bool    ecs_view_maybe_track(EcsView*, EcsArchetypeId, BitSet mask);
