#include "core_dynarray.h"
#include "ecs_meta.h"

typedef struct {
  String name;
  usize  size;
  usize  align;
} EcsCompMeta;

struct sEcsMeta {
  DynArray   components; // EcsCompMeta[]
  Allocator* alloc;
};

/*
 * Pointer only remains stable while no new components are registered.
 */
const EcsCompMeta* ecs_comp_meta(EcsMeta*, EcsCompId);
