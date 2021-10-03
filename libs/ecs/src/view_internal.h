#include "core_bitset.h"
#include "core_dynarray.h"
#include "ecs_view.h"

#include "def_internal.h"
#include "storage_internal.h"

struct sEcsView {
  BitSet   filterWith, filterWithout;
  BitSet   accessRead, accessWrite;
  DynArray archetypes; // EcsArchetypeId[].
};

EcsView ecs_view_create(Allocator*, const EcsDef*, const EcsViewDef*);
void    ecs_view_destroy(Allocator*, const EcsDef*, EcsView*);
bool    ecs_view_matches(const EcsView*, BitSet mask);
bool    ecs_view_maybe_track(EcsView*, EcsArchetypeId, BitSet mask);
