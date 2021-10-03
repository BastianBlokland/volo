#include "core_diag.h"
#include "ecs_iterator.h"

EcsIterator* ecs_iterator_impl_create(Mem itrMem, BitSet mask) {
  const usize extraMem  = itrMem.size - sizeof(EcsIterator);
  const usize compCount = extraMem / sizeof(Mem);

  diag_assert(bitset_count(mask) == compCount);

  EcsIterator* itr = mem_as_t(itrMem, EcsIterator);
  *itr             = (EcsIterator){
      .mask      = mask,
      .compCount = compCount,
  };
  return itr;
}
