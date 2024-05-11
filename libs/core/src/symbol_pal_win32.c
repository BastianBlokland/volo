#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_path.h"
#include "core_search.h"
#include "core_thread.h"

#include "symbol_internal.h"

#include <Windows.h>
#include <psapi.h>

// #define VOLO_SYMBOL_RESOLVER_VERBOSE

#define symbol_aux_chunk_size (4 * usize_kibibyte)
#define symbol_name_length_max 64

typedef struct {
  SymbolAddrRel begin, end;
  String        name;
} SymInfo;

/**
 * To lookup symbol debug information we use the DbgHelp library.
 * NOTE: Is only available when a PDB file is found or debug symbols are embedded in the executable.
 */

#define dbghelp_symtag_function 5

// NOTE: Needs to match 'struct SYMBOL_INFO ' from 'DbgHelp.h'.
typedef struct {
  ULONG   SizeOfStruct;
  ULONG   TypeIndex;
  ULONG64 Reserved[2];
  ULONG   Index;
  ULONG   Size;
  ULONG64 ModBase;
  ULONG   Flags;
  ULONG64 Value;
  ULONG64 Address;
  ULONG   Register;
  ULONG   Scope;
  ULONG   Tag;
  ULONG   NameLen;
  ULONG   MaxNameLen;
  CHAR    Name[1];
} DbgHelpSymInfo;

typedef BOOL(SYS_DECL* DbgHelpSymEnumCallback)(const DbgHelpSymInfo*, ULONG size, void* ctx);

typedef enum {
  SymResolver_Init,
  SymResolver_Ready,
  SymResolver_Error,
} SymResolverState;

typedef struct {
  Allocator*       alloc;
  Allocator*       allocAux; // (chunked) bump allocator for axillary data (eg symbol names).
  SymResolverState state;
  HANDLE           process;

  DynArray syms; // SymInfo[], kept sorted on address.

  DynLib*    dbgHelp;
  bool       dbgHelpActive;
  SymbolAddr dbgHelpBaseAddr; // NOTE: Does not match program base when using ASLR.

  // clang-format off
  BOOL    (SYS_DECL* SymInitialize)(HANDLE process, PCSTR userSearchPath, BOOL invadeProcess);
  BOOL    (SYS_DECL* SymCleanup)(HANDLE process);
  DWORD   (SYS_DECL* SymSetOptions)(DWORD options);
  DWORD64 (SYS_DECL* SymLoadModuleEx)(HANDLE process, HANDLE file, PCSTR imageName, PCSTR moduleName, DWORD64 baseOfDll, DWORD dllSize, void* data, DWORD flags);
  BOOL    (SYS_DECL* SymEnumSymbolsEx)(HANDLE process, ULONG64 baseOfDll, PCSTR mask, DbgHelpSymEnumCallback callback, void* ctx, DWORD options);
  // clang-format on
} SymResolver;

static SymResolver* g_symResolver;
static ThreadMutex  g_symResolverMutex;

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static i8 resolver_sym_compare(const void* a, const void* b) {
  return compare_u32(field_ptr(a, SymInfo, begin), field_ptr(b, SymInfo, begin));
}

static bool resolver_sym_contains(const SymInfo* sym, const SymbolAddrRel addr) {
  return addr >= sym->begin && addr < sym->end;
}

static void resolver_sym_register(
    SymResolver* r, const SymbolAddrRel begin, const SymbolAddrRel end, const String name) {
  const SymInfo sym = {.begin = begin, .end = end, .name = string_dup(r->allocAux, name)};
  *dynarray_insert_sorted_t(&r->syms, SymInfo, resolver_sym_compare, &sym) = sym;

#ifdef VOLO_SYMBOL_RESOLVER_VERBOSE
  diag_print(
      "[{}:{}] {}\n",
      fmt_int(begin, .base = 16, .minDigits = 6),
      fmt_int(end, .base = 16, .minDigits = 6),
      fmt_text(name));
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

static bool resolver_dbghelp_load(SymResolver* r) {
  DynLibResult loadRes = dynlib_load(r->alloc, string_lit("Dbghelp.dll"), &r->dbgHelp);
  if (loadRes != DynLibResult_Success) {
    return false;
  }

#define DBG_LOAD_SYM(_NAME_)                                                                       \
  do {                                                                                             \
    r->_NAME_ = dynlib_symbol(r->dbgHelp, string_lit(#_NAME_));                                    \
    if (!r->_NAME_) {                                                                              \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  DBG_LOAD_SYM(SymInitialize);
  DBG_LOAD_SYM(SymCleanup);
  DBG_LOAD_SYM(SymSetOptions);
  DBG_LOAD_SYM(SymLoadModuleEx);
  DBG_LOAD_SYM(SymEnumSymbolsEx);

  return true;
}

/**
 * Debug info search path.
 * NOTE: We only include the executable's own directory.
 */
static const char* resolver_dbghelp_searchpath() {
  const String execParentPath = path_parent(g_path_executable);
  return to_null_term_scratch(execParentPath);
}

static DWORD resolver_dbghelp_options() {
  DWORD options = 0;
  options |= 0x00000004; /* SYMOPT_DEFERRED_LOADS */
  options |= 0x00000200; /* SYMOPT_FAIL_CRITICAL_ERRORS */
  options |= 0x00000008; /* SYMOPT_NO_CPP */
  options |= 0x00080000; /* SYMOPT_NO_PROMPTS */
  options |= 0x00000100; /* SYMOPT_NO_UNQUALIFIED_LOADS */
  options |= 0x00000002; /* SYMOPT_UNDNAME */
  return options;
}

static bool resolver_dbghelp_begin(SymResolver* r) {
  diag_assert(r->state == SymResolver_Init);
  const BOOL invadeProcess = false; // Do not automatically load dbg-info for all modules.
  if (!r->SymInitialize(r->process, resolver_dbghelp_searchpath(), invadeProcess)) {
    return false;
  }
  r->SymSetOptions(resolver_dbghelp_options());

  const char* imageName = to_null_term_scratch(g_path_executable);
  r->dbgHelpActive      = true;
  r->dbgHelpBaseAddr    = r->SymLoadModuleEx(r->process, null, imageName, null, 0, 0, null, 0);
  return r->dbgHelpBaseAddr != 0;
}

static void resolver_dbghelp_end(SymResolver* r) {
  diag_assert(r->dbgHelpActive);
  r->SymCleanup(r->process);
  r->dbgHelpActive = false;
}

static BOOL SYS_DECL
resolver_dbghelp_enum_proc(const DbgHelpSymInfo* info, const ULONG size, void* ctx) {
  (void)size;
  SymResolver* r = ctx;
  if (info->Tag != dbghelp_symtag_function) {
    goto Continue; // Only (non-inlined) function symbols are supported at this time.
  }
  if (!info->NameLen || info->NameLen > symbol_name_length_max) {
    goto Continue; // When static-linking the CRT we get long c++ symbol names we can ignore.
  }
  if (info->Address < r->dbgHelpBaseAddr) {
    goto Continue; // Symbol is outside of the executable space.
  }
  if (!size) {
    goto Continue;
  }
  const SymbolAddrRel addrBegin = (SymbolAddrRel)((SymbolAddr)info->Address - r->dbgHelpBaseAddr);
  const SymbolAddrRel addrEnd   = addrBegin + (SymbolAddrRel)size;
  const String        name      = mem_create(info->Name, info->NameLen);
  resolver_sym_register(r, addrBegin, addrEnd, name);

Continue:
  return TRUE; // Continue enumerating.
}

static bool resolver_init(SymResolver* r) {
  const DWORD                  options  = 1; /* SYMENUM_OPTIONS_DEFAULT */
  const DbgHelpSymEnumCallback callback = &resolver_dbghelp_enum_proc;
  if (r->SymEnumSymbolsEx(r->process, r->dbgHelpBaseAddr, "*", callback, r, options)) {
    return true;
  }
  return false;
}

static SymResolver* resolver_create(Allocator* alloc) {
  SymResolver* r = alloc_alloc_t(alloc, SymResolver);

  *r = (SymResolver){
      .alloc    = alloc,
      .allocAux = alloc_chunked_create(alloc, alloc_bump_create, symbol_aux_chunk_size),
      .process  = GetCurrentProcess(),
      .syms     = dynarray_create_t(alloc, SymInfo, 1024),
  };

  if (!resolver_dbghelp_load(r)) {
    goto Error;
  }
  if (!resolver_dbghelp_begin(r)) {
    goto Error;
  }
  if (!resolver_init(r)) {
    goto Error;
  }
  resolver_dbghelp_end(r);
  r->state = SymResolver_Ready;
  return r;

Error:
  r->state = SymResolver_Error;
  return r;
}

static void resolver_destroy(SymResolver* r) {
  dynarray_destroy(&r->syms);
  if (r->dbgHelpActive) {
    resolver_dbghelp_end(r);
  }
  if (r->dbgHelp) {
    dynlib_destroy(r->dbgHelp);
  }
  alloc_chunked_destroy(r->allocAux);
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

SymbolAddr symbol_pal_prog_begin(void) {
  extern const u8 __ImageBase[]; // Provided by the linker script.
  return (SymbolAddr)&__ImageBase;
}

SymbolAddr symbol_pal_prog_end(void) {
  HANDLE           process      = GetCurrentProcess();
  const SymbolAddr programBegin = symbol_pal_prog_begin();

  MODULEINFO moduleInfo;
  GetModuleInformation(process, (HMODULE)programBegin, &moduleInfo, sizeof(MODULEINFO));

  return programBegin + (SymbolAddr)moduleInfo.SizeOfImage;
}

String symbol_pal_dbg_name(const SymbolAddrRel addr) {
  String result = string_empty;
  thread_mutex_lock(g_symResolverMutex);
  {
    if (!g_symResolver) {
      g_symResolver = resolver_create(g_alloc_heap);
    }
    const SymInfo* info = resolver_lookup(g_symResolver, addr);
    if (info) {
      result = info->name;
    }
  }
  thread_mutex_unlock(g_symResolverMutex);
  return result;
}

SymbolAddrRel symbol_pal_dbg_base(const SymbolAddrRel addr) {
  SymbolAddrRel result = sentinel_u32;
  thread_mutex_lock(g_symResolverMutex);
  {
    if (!g_symResolver) {
      g_symResolver = resolver_create(g_alloc_heap);
    }
    const SymInfo* info = resolver_lookup(g_symResolver, addr);
    if (info) {
      result = info->begin;
    }
  }
  thread_mutex_unlock(g_symResolverMutex);
  return result;
}
