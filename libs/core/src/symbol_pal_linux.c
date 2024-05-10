#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_file.h"
#include "core_path.h"
#include "core_search.h"
#include "core_thread.h"

#include "file_internal.h"
#include "symbol_internal.h"

// #define VOLO_SYMBOL_RESOLVER_VERBOSE

typedef struct {
  SymbolAddrRel begin, end;
  const char*   name;
} SymInfo;

/**
 * To lookup symbol debug information we parse the DWARF data in the ELF executable.
 * For parsing the DWARF data we rely on 'libdw' which is pre-installed on most linux distributions.
 * NOTE: DWARF info is only available if the executable was build with '-g' and not stripped.
 */

#define elf_ptype_load 1
#define dwarf_cmd_read 0
#define dwarf_tag_entrypoint 0x03
#define dwarf_tag_subprogram 0x2e

typedef struct sElf Elf;

typedef struct {
  u32  type;
  u32  flags;
  uptr offset;
  uptr vaddr;
  uptr paddr;
  u64  filesz;
  u64  memsz;
  u64  align;
} ElfPHeader;

typedef struct sDwarf       Dwarf;
typedef struct sDwarfCu     DwarfCu;
typedef struct sDwarfAbbrev DwarfAbbrev;

typedef struct {
  void*        addr;
  DwarfCu*     cu;
  DwarfAbbrev* abbrev;
  long int     padding;
} DwarfDie;

typedef enum {
  SymResolver_Init,
  SymResolver_Ready,
  SymResolver_Error,
} SymResolverState;

typedef struct {
  Allocator*       alloc;
  SymResolverState state;
  File*            exec; // Handle to our own executable.

  DynArray syms; // SymInfo[], kept sorted on begin address.

  DynLib* dwLib;
  Dwarf*  dwSession;

  // clang-format off
  Dwarf*      (SYS_DECL* dwarf_begin)(int fildes, int cmd);
  int         (SYS_DECL* dwarf_end)(Dwarf*);
  Elf*        (SYS_DECL* dwarf_getelf)(Dwarf*);
  int         (SYS_DECL* dwarf_nextcu)(Dwarf*, u64 off, u64* nextOff, usize* headerSize, u64* abbrevOffset, u8* addressSize, u8* offsetSize);
  DwarfDie*   (SYS_DECL* dwarf_offdie)(Dwarf*, u64 off, DwarfDie* result);
  int         (SYS_DECL* dwarf_child)(DwarfDie*, DwarfDie* result);
  int         (SYS_DECL* dwarf_lowpc)(DwarfDie*, uptr* result);
  int         (SYS_DECL* dwarf_highpc)(DwarfDie*, uptr* result);
  int         (SYS_DECL* dwarf_siblingof)(DwarfDie*, DwarfDie* result);
  int         (SYS_DECL* dwarf_tag)(DwarfDie*);
  const char* (SYS_DECL* dwarf_diename)(DwarfDie*);
  int         (SYS_DECL* elf_getphdrnum)(Elf*, usize* result);
  ElfPHeader* (SYS_DECL* gelf_getphdr)(Elf*, int index, ElfPHeader* result);
  // clang-format on
} SymResolver;

static SymResolver* g_symResolver;
static ThreadMutex  g_symResolverMutex;

static i8 resolver_sym_compare(const void* a, const void* b) {
  return compare_u32(field_ptr(a, SymInfo, begin), field_ptr(b, SymInfo, begin));
}

static bool resolver_sym_contains(const SymInfo* sym, const SymbolAddrRel addr) {
  return addr >= sym->begin && addr < sym->end;
}

static void resolver_sym_register(
    SymResolver* r, const SymbolAddrRel begin, const SymbolAddrRel end, const char* name) {
  const SymInfo sym = {.begin = begin, .end = end, .name = name};
  *dynarray_insert_sorted_t(&r->syms, SymInfo, resolver_sym_compare, &sym) = sym;

#ifdef VOLO_SYMBOL_RESOLVER_VERBOSE
  diag_print(
      "[{}:{}] {}\n",
      fmt_int(begin, .base = 16, .minDigits = 6),
      fmt_int(end, .base = 16, .minDigits = 6),
      fmt_text(string_from_null_term(name)));
#endif
}

static const SymInfo* resolver_sym_find(SymResolver* r, const SymbolAddrRel addr) {
  if (!r->syms.size) {
    return null; // No symbols known.
  }
  const SymInfo* begin = dynarray_begin_t(&r->syms, SymInfo);
  const SymInfo* end   = dynarray_end_t(&r->syms, SymInfo);

  const SymInfo  tgt     = {.begin = addr};
  const SymInfo* greater = search_binary_greater_t(begin, end, SymInfo, resolver_sym_compare, &tgt);
  const SymInfo* greaterOrEnd = greater ? greater : end;
  const SymInfo* candidate    = greaterOrEnd - 1;

  return resolver_sym_contains(candidate, addr) ? candidate : null;
}

static bool resolver_dw_load(SymResolver* r) {
  DynLibResult loadRes = dynlib_load(r->alloc, string_lit("libdw.so.1"), &r->dwLib);
  if (loadRes != DynLibResult_Success) {
    return false;
  }

#define DW_LOAD_SYM(_NAME_)                                                                        \
  do {                                                                                             \
    r->_NAME_ = dynlib_symbol(r->dwLib, string_lit(#_NAME_));                                      \
    if (!r->_NAME_) {                                                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  DW_LOAD_SYM(dwarf_begin);
  DW_LOAD_SYM(dwarf_end);
  DW_LOAD_SYM(dwarf_getelf);
  DW_LOAD_SYM(dwarf_nextcu);
  DW_LOAD_SYM(dwarf_offdie);
  DW_LOAD_SYM(dwarf_child);
  DW_LOAD_SYM(dwarf_lowpc);
  DW_LOAD_SYM(dwarf_highpc);
  DW_LOAD_SYM(dwarf_siblingof);
  DW_LOAD_SYM(dwarf_tag);
  DW_LOAD_SYM(dwarf_diename);
  DW_LOAD_SYM(elf_getphdrnum);
  DW_LOAD_SYM(gelf_getphdr);

  return true;
}

static bool resolver_dw_begin(SymResolver* r) {
  diag_assert(r->state == SymResolver_Init);
  diag_assert(!r->dwSession);
  r->dwSession = r->dwarf_begin(r->exec->handle, dwarf_cmd_read);
  return r->dwSession != null;
}

static void resolver_dw_end(SymResolver* r) {
  diag_assert(r->dwSession);
  r->dwarf_end(r->dwSession);
}

/**
 * Find the virtual base address of the elf executable (lowest mapped segment of the executable).
 * NOTE: This does not necessarily match the actual '__executable_start' if address layout
 * randomization is used, when using randomization the ELF base address is usually zero.
 */
static bool resolver_addr_base(SymResolver* r, SymbolAddr* out) {
  Elf*  elf = r->dwarf_getelf(r->dwSession);
  usize pHeaderCount;
  if (r->elf_getphdrnum(elf, &pHeaderCount)) {
    return false;
  }
  for (usize i = 0; i != pHeaderCount; ++i) {
    ElfPHeader header;
    if (r->gelf_getphdr(elf, (int)i, &header)) {
      if (header.type == elf_ptype_load) {
        *out = header.vaddr; // Use the first loaded segment as the base.
        return true;
      }
    }
  }
  return false;
}

static bool resolver_init(SymResolver* r) {
  /**
   * Find all the (non-inlined) function symbols in all the compilation units.
   * NOTE: Doesn't depend on 'aranges' dwarf info as that is optional and clang does not emit it.
   */
  SymbolAddr addrBase;
  if (!resolver_addr_base(r, &addrBase)) {
    return false;
  }
  u64    cuOffset = 0;
  u64    cuOffsetNext;
  size_t cuSize;
  // Iterate over all compilation units.
  while (!r->dwarf_nextcu(r->dwSession, cuOffset, &cuOffsetNext, &cuSize, 0, 0, 0)) {
    DwarfDie cu;
    if (!r->dwarf_offdie(r->dwSession, cuOffset + cuSize, &cu)) {
      continue;
    }
    // Walk over all the children (functions) in the compilation unit.
    DwarfDie child;
    if (r->dwarf_child(&cu, &child) == 0) {
      do {
        const int tag = r->dwarf_tag(&child);
        if (tag != dwarf_tag_entrypoint && tag != dwarf_tag_subprogram) {
          continue; // Only (non-inlined) function symbols are supported at this time.
        }
        const char* funcName = r->dwarf_diename(&child);
        if (!funcName) {
          continue; // Function is missing a name.
        }
        SymbolAddr addrBegin, addrEnd;
        if (r->dwarf_lowpc(&child, &addrBegin) == -1) {
          continue; // Function is missing an lowpc address.
        }
        if (r->dwarf_highpc(&child, &addrEnd) == -1) {
          continue; // Function is missing an highpc address.
        }
        if (addrBegin < addrBase || addrEnd < addrBegin) {
          continue; // Invalid address, this would mean a corrupt elf file.
        }
        const SymbolAddrRel addrBeginRel = (SymbolAddrRel)(addrBegin - addrBase);
        const SymbolAddrRel addrEndRel   = (SymbolAddrRel)(addrEnd - addrBase);
        resolver_sym_register(r, addrBeginRel, addrEndRel, funcName);

      } while (r->dwarf_siblingof(&child, &child) == 0);
    }
    cuOffset = cuOffsetNext; // Iterate to the next compilation unit offset.
  }
  return true;
}

static SymResolver* resolver_create(Allocator* alloc) {
  SymResolver* r = alloc_alloc_t(alloc, SymResolver);

  *r = (SymResolver){
      .alloc = alloc,
      .syms  = dynarray_create_t(alloc, SymInfo, 1024),
  };

  if (file_create(alloc, g_path_executable, FileMode_Open, FileAccess_Read, &r->exec)) {
    goto Error;
  }
  if (!resolver_dw_load(r)) {
    goto Error;
  }
  if (!resolver_dw_begin(r)) {
    goto Error;
  }
  if (!resolver_init(r)) {
    goto Error;
  }
  r->state = SymResolver_Ready;
  return r;

Error:
  r->state = SymResolver_Error;
  return r;
}

static void resolver_destroy(SymResolver* r) {
  dynarray_destroy(&r->syms);
  if (r->dwSession) {
    resolver_dw_end(r);
  }
  if (r->exec) {
    file_destroy(r->exec);
  }
  if (r->dwLib) {
    dynlib_destroy(r->dwLib);
  }
  alloc_free_t(r->alloc, r);
}

static const SymInfo* resolver_lookup(SymResolver* r, const SymbolAddrRel addr) {
  if (r->state != SymResolver_Ready) {
    return null;
  }
  return resolver_sym_find(r, addr);
}

void symbol_pal_init(void) { g_symResolverMutex = thread_mutex_create(g_alloc_persist); }

void symbol_pal_teardown(void) {
  if (g_symResolver) {
    resolver_destroy(g_symResolver);
  }
  thread_mutex_destroy(g_symResolverMutex);
}

SymbolAddr symbol_pal_program_begin(void) {
  extern const u8 __executable_start[]; // Provided by the linker script.
  return (SymbolAddr)&__executable_start;
}

SymbolAddr symbol_pal_program_end(void) {
  extern const u8 __etext[]; // Provided by the linker script.
  return (SymbolAddr)&__etext;
}

String symbol_pal_name(const SymbolAddrRel addr) {
  String result = string_empty;
  thread_mutex_lock(g_symResolverMutex);
  {
    if (!g_symResolver) {
      g_symResolver = resolver_create(g_alloc_heap);
    }
    const SymInfo* info = resolver_lookup(g_symResolver, addr);
    if (info) {
      result = string_from_null_term(info->name);
    }
  }
  thread_mutex_unlock(g_symResolverMutex);
  return result;
}
