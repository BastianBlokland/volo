#include "core_alloc.h"

#include "disassembler_internal.h"

typedef struct sRvkDisassembler {
  Allocator* alloc;
} RvkDisassembler;

RvkDisassembler* rvk_disassembler_create(Allocator* alloc) {
  RvkDisassembler* disassembler = alloc_alloc_t(alloc, RvkDisassembler);

  *disassembler = (RvkDisassembler){
      .alloc = alloc,
  };

  return disassembler;
}

void rvk_disassembler_destroy(RvkDisassembler* dis) { alloc_free_t(dis->alloc, dis); }

RvkDisassemblerResult
rvk_disassembler_spirv_to_text(const RvkDisassembler* dis, const String in, DynString* out) {
  (void)dis;
  (void)in;
  (void)out;
  return RvkDisassembler_DependenciesNotFound;
}
