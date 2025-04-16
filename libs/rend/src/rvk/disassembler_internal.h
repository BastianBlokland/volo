#pragma once
#include "core.h"

typedef enum {
  RvkDisassembler_Success = 0,
  RvkDisassembler_Unavailable,
  RvkDisassembler_InvalidAssembly,
} RvkDisassemblerResult;

typedef struct sRvkDisassembler RvkDisassembler;

RvkDisassembler* rvk_disassembler_create(Allocator*);
void             rvk_disassembler_destroy(RvkDisassembler*);

/**
 * Disassemble the given spir-v assembly to human readable text.
 */
RvkDisassemblerResult
rvk_disassembler_spirv_to_text(const RvkDisassembler*, String in, DynString* out);
