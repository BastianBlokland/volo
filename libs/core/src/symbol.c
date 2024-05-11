#include "core_array.h"
#include "core_sentinel.h"

#include "symbol_internal.h"

static SymbolAddr g_symProgramBegin;
static SymbolAddr g_symProgramEnd;

void symbol_init(void) {
  symbol_pal_init();

  g_symProgramBegin = symbol_pal_program_begin();
  g_symProgramEnd   = symbol_pal_program_end();
}

void symbol_teardown(void) { symbol_pal_teardown(); }

NO_INLINE_HINT SymbolStack symbol_stack(void) {
  ASSERT(sizeof(uptr) == 8, "Only 64 bit architectures are supported at the moment")

  SymbolStack stack;
  u32         frameIndex = 0;

#if defined(VOLO_CLANG) || defined(VOLO_GCC)
  /**
   * Walk the stack using the frame-pointer stored in the RBP register on x86_64.
   * NOTE: Only x86_64 is supported at the moment.
   * NOTE: Requires the binary to be compiled with frame-pointers.
   */
  struct Frame {
    const struct Frame* prev;
    SymbolAddr          retAddr;
  };
  ASSERT(sizeof(struct Frame) == sizeof(uptr) * 2, "Unexpected Frame size");

  // Retrieve the frame-pointer from the EBP register.
  const struct Frame* fp;
  asm("movq %%rbp, %[fp]" : [fp] "=r"(fp));

  // Fill the stack by walking the linked-list of frames.
  for (; fp; fp = fp->prev) {
    if (!symbol_addr_valid(fp->retAddr)) {
      continue; // Function does not belong to our executable.
    }
    stack.frames[frameIndex++] = symbol_addr_rel(fp->retAddr);
    if (frameIndex == array_elems(stack.frames)) {
      break; // Reached the stack-frame limit.
    }
  }
#elif defined(VOLO_MSVC)

#else
  ASSERT(false, "Unsupported compiler");
#endif

  // Set the remaining frames to a sentinel value.
  for (; frameIndex != array_elems(stack.frames); ++frameIndex) {
    stack.frames[frameIndex] = sentinel_u32;
  }

  return stack;
}

bool symbol_addr_valid(const SymbolAddr symbol) {
  return symbol >= g_symProgramBegin && symbol < g_symProgramEnd;
}

SymbolAddrRel symbol_addr_rel(const SymbolAddr symbol) {
  if (!symbol_addr_valid(symbol)) {
    return sentinel_u32;
  }
  return (SymbolAddrRel)(symbol - g_symProgramBegin);
}

SymbolAddr symbol_addr_abs(const SymbolAddrRel addr) {
  if (sentinel_check(addr)) {
    return 0;
  }
  return (SymbolAddr)addr + g_symProgramBegin;
}

String symbol_name(const SymbolAddr addr) {
  if (!symbol_addr_valid(addr)) {
    return string_empty;
  }
  const SymbolAddrRel addrRel = symbol_addr_rel(addr);
  return symbol_pal_name(addrRel);
}

String symbol_name_rel(const SymbolAddrRel addr) {
  if (sentinel_check(addr)) {
    return string_empty;
  }
  return symbol_pal_name(addr);
}

SymbolAddrRel symbol_base(const SymbolAddrRel addr) {
  if (sentinel_check(addr)) {
    return sentinel_u32;
  }
  return symbol_pal_base(addr);
}
