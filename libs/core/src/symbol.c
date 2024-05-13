#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_dynarray.h"
#include "core_file.h"
#include "core_format.h"
#include "core_math.h"
#include "core_search.h"
#include "core_sentinel.h"
#include "core_thread.h"

#include "symbol_internal.h"

#if defined(VOLO_WIN32)
#include <Windows.h>
#endif

// #define VOLO_SYMBOL_VERBOSE

#define symbol_reg_name_max 64
#define symbol_reg_aux_chunk_size (4 * usize_kibibyte)

typedef struct {
  SymbolAddrRel begin, end;
  String        name;
} SymbolInfo;

struct sSymbolReg {
  Allocator* alloc;
  Allocator* allocAux; // (chunked) bump allocator for axillary data (eg symbol names).
  DynArray   syms;     // SymbolInfo[], kept sorted on begin address.
};

static bool        g_symInit;
static SymbolAddr  g_symProgBegin;
static SymbolAddr  g_symProgEnd;
static SymbolReg*  g_symReg;
static ThreadMutex g_symRegMutex;

static i8 sym_info_compare(const void* a, const void* b) {
  return compare_u32(field_ptr(a, SymbolInfo, begin), field_ptr(b, SymbolInfo, begin));
}

INLINE_HINT static bool sym_addr_valid(const SymbolAddr symbol) {
  if (!g_symInit) {
    return false; // Program addresses not yet initalized; can happen when calling this during init.
  }
  // NOTE: Only includes the executable itself, not dynamic libraries.
  return symbol >= g_symProgBegin && symbol < g_symProgEnd;
}

INLINE_HINT static SymbolAddrRel sym_addr_rel(const SymbolAddr symbol) {
  if (!sym_addr_valid(symbol)) {
    return (SymbolAddrRel)sentinel_u32;
  }
  return (SymbolAddrRel)(symbol - g_symProgBegin);
}

INLINE_HINT static SymbolAddr sym_addr_abs(const SymbolAddrRel addr) {
  if (sentinel_check(addr)) {
    return (SymbolAddr)sentinel_uptr;
  }
  return (SymbolAddr)addr + g_symProgBegin;
}

static bool sym_info_contains(const SymbolInfo* sym, const SymbolAddrRel addr) {
  return addr >= sym->begin && addr < sym->end;
}

static SymbolReg* symbol_reg_create(Allocator* alloc) {
  SymbolReg* r = alloc_alloc_t(alloc, SymbolReg);

  *r = (SymbolReg){
      .alloc    = alloc,
      .allocAux = alloc_chunked_create(alloc, alloc_bump_create, symbol_reg_aux_chunk_size),
      .syms     = dynarray_create_t(alloc, SymbolInfo, 2048),
  };

  return r;
}

static void symbol_reg_destroy(SymbolReg* r) {
  dynarray_destroy(&r->syms);
  alloc_chunked_destroy(r->allocAux);
  alloc_free_t(r->alloc, r);
}

/**
 * Find information for the symbol that contains the given address.
 * NOTE: Retrieved pointer is valid until a new entry is added.
 * NOTE: Retrieved symbol name is valid until teardown.
 */
static const SymbolInfo* symbol_reg_query(const SymbolReg* r, const SymbolAddrRel addr) {
  if (!r->syms.size) {
    return null; // No symbols known.
  }
  const SymbolInfo* begin = dynarray_begin_t(&r->syms, SymbolInfo);
  const SymbolInfo* end   = dynarray_end_t(&r->syms, SymbolInfo);

  const SymbolInfo  tgt = {.begin = addr};
  const SymbolInfo* gtr = search_binary_greater_t(begin, end, SymbolInfo, sym_info_compare, &tgt);
  if (gtr == begin) {
    return null; // Address is before the lowest address symbol.
  }
  const SymbolInfo* gtOrEnd   = gtr ? gtr : end;
  const SymbolInfo* candidate = gtOrEnd - 1;
  return sym_info_contains(candidate, addr) ? candidate : null;
}

static void symbol_reg_dump(const SymbolReg* r, DynString* out) {
  fmt_write(out, "Debug symbols:\n");
  dynarray_for_t(&r->syms, SymbolInfo, info) {
    const SymbolAddrRel size = info->end - info->begin;
    fmt_write(
        out,
        " {} {} +{}\n",
        fmt_int(info->begin, .base = 16, .minDigits = 8),
        fmt_text(info->name),
        fmt_int(size));
  }
}

MAYBE_UNUSED static void symbol_reg_dump_out(const SymbolReg* r) {
  DynString str = dynstring_create(g_alloc_heap, 4 * usize_kibibyte);
  symbol_reg_dump(r, &str);
  file_write_sync(g_file_stdout, dynstring_view(&str));
  dynstring_destroy(&str);
}

static const SymbolReg* symbol_reg_get(void) {
  if (g_symReg) {
    return g_symReg;
  }
  static THREAD_LOCAL bool g_symRegInitializing;
  if (g_symRegInitializing) {
    /**
     * Handle 'symbol_reg_get' being called while we are currently creating the registry, this can
     * happen if we trigger an assert while building the registry for example.
     */
    return null;
  }
  g_symRegInitializing = true;
  thread_mutex_lock(g_symRegMutex);
  if (!g_symReg) {
    SymbolReg* reg = symbol_reg_create(g_alloc_heap);
    symbol_pal_dbg_init(reg);
#if defined(VOLO_SYMBOL_VERBOSE)
    symbol_reg_dump_out(reg);
#endif
    thread_atomic_fence();
    g_symReg = reg;
  }
  thread_mutex_unlock(g_symRegMutex);
  g_symRegInitializing = false;
  return g_symReg;
}

void symbol_reg_add(
    SymbolReg* r, const SymbolAddrRel begin, const SymbolAddrRel end, const String name) {
  const usize  nameSize   = math_min(name.size, symbol_reg_name_max);
  const String nameStored = string_dup(r->allocAux, string_slice(name, 0, nameSize));

  const SymbolInfo info = {.begin = begin, .end = end, .name = nameStored};
  *dynarray_insert_sorted_t(&r->syms, SymbolInfo, sym_info_compare, &info) = info;
}

void symbol_init(void) {
  g_symProgBegin = symbol_pal_prog_begin();
  g_symProgEnd   = symbol_pal_prog_end();
  g_symRegMutex  = thread_mutex_create(g_alloc_persist);
  g_symInit      = true;
}

void symbol_teardown(void) {
  g_symInit = false;
  if (g_symReg) {
    symbol_reg_destroy(g_symReg);
  }
  thread_mutex_destroy(g_symRegMutex);
}

NO_INLINE_HINT FLATTEN_HINT SymbolStack symbol_stack_walk(void) {
  ASSERT(sizeof(uptr) == 8, "Only 64 bit architectures are supported at the moment")

  SymbolStack stack;
  u32         frameIndex = 0;

#if defined(VOLO_WIN32)
  /**
   * Walk the stack using the x64 unwind tables.
   * NOTE: Win32 x86_64 ABI rarely uses a frame-pointer unfortunately.
   * Docs: https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64
   * Ref: http://www.nynaeve.net/Code/StackWalk64.cpp
   */
  CONTEXT                       unwindCtx;
  KNONVOLATILE_CONTEXT_POINTERS unwindNvCtx;
  PRUNTIME_FUNCTION             unwindFunc;
  DWORD64                       unwindImageBase;
  PVOID                         unwindHandlerData;
  ULONG_PTR                     unwindEstablisherFrame;

  RtlCaptureContext(&unwindCtx);

  for (;;) {
    unwindFunc = RtlLookupFunctionEntry(unwindCtx.Rip, &unwindImageBase, NULL);
    RtlZeroMemory(&unwindNvCtx, sizeof(KNONVOLATILE_CONTEXT_POINTERS));
    if (!unwindFunc) {
      // Function has no unwind-data, must be a leaf-function; adjust the stack accordingly.
      unwindCtx.Rip = (ULONG64)(*(PULONG64)unwindCtx.Rsp);
      unwindCtx.Rsp += 8;
    } else {
      // Unwind to the caller function.
      RtlVirtualUnwind(
          UNW_FLAG_NHANDLER,
          unwindImageBase,
          unwindCtx.Rip,
          unwindFunc,
          &unwindCtx,
          &unwindHandlerData,
          &unwindEstablisherFrame,
          &unwindNvCtx);
    }
    if (!unwindCtx.Rip) {
      break; // Reached the end of the call-stack.
    }
    const SymbolAddrRel addrRel = sym_addr_rel(unwindCtx.Rip);
    if (sentinel_check(addrRel)) {
      continue; // Function does not belong to our executable.
    }
    stack.frames[frameIndex++] = addrRel;
    if (frameIndex == array_elems(stack.frames)) {
      break; // Reached the stack-frame limit.
    }
  }
#else
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
  asm volatile("movq %%rbp, %[fp]" : [fp] "=r"(fp)); // Volatile to avoid compiler reordering.

  // Fill the stack by walking the linked-list of frames.
  for (; fp && bits_aligned_ptr(fp, sizeof(uptr)); fp = fp->prev) {
    const SymbolAddrRel addrRel = sym_addr_rel(fp->retAddr);
    if (sentinel_check(addrRel)) {
      continue; // Function does not belong to our executable.
    }
    stack.frames[frameIndex++] = addrRel;
    if (frameIndex == array_elems(stack.frames)) {
      break; // Reached the stack-frame limit.
    }
  }
#endif

  // Set the remaining frames to a sentinel value.
  for (; frameIndex != array_elems(stack.frames); ++frameIndex) {
    stack.frames[frameIndex] = sentinel_u32;
  }

  return stack;
}

void symbol_stack_write(const SymbolStack* stack, DynString* out) {
  const SymbolReg* reg = symbol_reg_get();

  fmt_write(out, "Stack:\n");
  for (u32 frameIndex = 0; frameIndex != array_elems(stack->frames); ++frameIndex) {
    const SymbolAddrRel addr = stack->frames[frameIndex];
    if (sentinel_check(addr)) {
      break; // End of stack.
    }
    const SymbolInfo* info = reg ? symbol_reg_query(reg, addr) : null;
    if (info) {
      const u32 offset = addr - info->begin;
      fmt_write(
          out,
          " {}) {} {} +{}\n",
          fmt_int(frameIndex),
          fmt_int(info->begin, .base = 16, .minDigits = 8),
          fmt_text(info->name),
          fmt_int(offset));
    } else {
      const SymbolAddr addrAbs = symbol_addr_abs(addr);
      fmt_write(
          out,
          " {}) {} {}\n",
          fmt_int(frameIndex),
          fmt_int(addr, .base = 16, .minDigits = 8),
          fmt_int(addrAbs, .base = 16, .minDigits = 16));
    }
  }
}

SymbolAddrRel symbol_addr_rel(const SymbolAddr addr) { return sym_addr_rel(addr); }
SymbolAddrRel symbol_addr_rel_ptr(const Symbol symbol) { return sym_addr_rel((SymbolAddr)symbol); }
SymbolAddr    symbol_addr_abs(const SymbolAddrRel addr) { return sym_addr_abs(addr); }

String symbol_dbg_name(const SymbolAddrRel addr) {
  const SymbolReg* reg = symbol_reg_get();
  if (UNLIKELY(!reg || sentinel_check(addr))) {
    return string_empty;
  }
  const SymbolInfo* info = symbol_reg_query(reg, addr);
  return info ? info->name : string_empty;
}

SymbolAddrRel symbol_dbg_base(const SymbolAddrRel addr) {
  const SymbolReg* reg = symbol_reg_get();
  if (UNLIKELY(!reg || sentinel_check(addr))) {
    return sentinel_u32;
  }
  const SymbolInfo* info = symbol_reg_query(reg, addr);
  return info ? info->begin : sentinel_u32;
}
