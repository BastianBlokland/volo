#include "core_alloc.h"
#include "core_dynlib.h"
#include "core_path.h"

#include "symbol_internal.h"

#include <Windows.h>
#include <psapi.h>

/**
 * To retrieve symbol debug information we use the DbgHelp library.
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

typedef struct {
  HANDLE process;

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
} SymDbg;

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static bool sym_dbg_lib_load(SymDbg* dbg, Allocator* alloc) {
  DynLibResult loadRes = dynlib_load(alloc, string_lit("Dbghelp.dll"), &dbg->dbgHelp);
  if (loadRes != DynLibResult_Success) {
    return false;
  }

#define DBG_LOAD_SYM(_NAME_)                                                                       \
  do {                                                                                             \
    dbg->_NAME_ = dynlib_symbol(dbg->dbgHelp, string_lit(#_NAME_));                                \
    if (!dbg->_NAME_) {                                                                            \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  DBG_LOAD_SYM(SymInitialize);
  DBG_LOAD_SYM(SymCleanup);
  DBG_LOAD_SYM(SymSetOptions);
  DBG_LOAD_SYM(SymLoadModuleEx);
  DBG_LOAD_SYM(SymEnumSymbolsEx);

#undef DBG_LOAD_SYM

  return true;
}

/**
 * Debug info search path.
 * NOTE: We only include the executable's own directory.
 */
static const char* sym_dbg_searchpath() {
  const String execParentPath = path_parent(g_pathExecutable);
  return to_null_term_scratch(execParentPath);
}

static DWORD sym_dbg_options() {
  DWORD options = 0;
  options |= 0x00000004; /* SYMOPT_DEFERRED_LOADS */
  options |= 0x00000200; /* SYMOPT_FAIL_CRITICAL_ERRORS */
  options |= 0x00000008; /* SYMOPT_NO_CPP */
  options |= 0x00080000; /* SYMOPT_NO_PROMPTS */
  options |= 0x00000100; /* SYMOPT_NO_UNQUALIFIED_LOADS */
  options |= 0x00000002; /* SYMOPT_UNDNAME */
  return options;
}

static bool sym_dbg_lib_begin(SymDbg* dbg) {
  const BOOL invadeProcess = false; // Do not automatically load dbg-info for all modules.
  if (!dbg->SymInitialize(dbg->process, sym_dbg_searchpath(), invadeProcess)) {
    return false;
  }
  dbg->SymSetOptions(sym_dbg_options());
  dbg->dbgHelpActive = true;

  const char* imageName = to_null_term_scratch(g_pathExecutable);
  dbg->dbgHelpBaseAddr  = dbg->SymLoadModuleEx(dbg->process, null, imageName, null, 0, 0, null, 0);
  return dbg->dbgHelpBaseAddr != 0;
}

static void sym_dbg_lib_end(SymDbg* dbg) {
  dbg->SymCleanup(dbg->process);
  dbg->dbgHelpActive = false;
}

typedef struct {
  SymDbg*    dbg;
  SymbolReg* reg;
} SymDbgEnumCtx;

static BOOL SYS_DECL sym_dbg_enum_proc(const DbgHelpSymInfo* info, const ULONG size, void* ctx) {
  (void)size;
  SymDbgEnumCtx*   enumCtx  = ctx;
  const SymbolAddr baseAddr = enumCtx->dbg->dbgHelpBaseAddr;
  if (info->Tag != dbghelp_symtag_function) {
    goto Continue; // Only (non-inlined) function symbols are supported at this time.
  }
  if (!info->NameLen) {
    goto Continue;
  }
  if (info->Address < baseAddr) {
    goto Continue; // Symbol is outside of the executable space.
  }
  if (!size) {
    goto Continue;
  }
  const SymbolAddrRel addrBegin = (SymbolAddrRel)((SymbolAddr)info->Address - baseAddr);
  const SymbolAddrRel addrEnd   = addrBegin + (SymbolAddrRel)size;

  symbol_reg_add(enumCtx->reg, addrBegin, addrEnd, mem_create(info->Name, info->NameLen));

Continue:
  return TRUE; // Continue enumerating.
}

static bool sym_dbg_query(SymDbg* dbg, SymbolReg* reg) {
  const DWORD                  options  = 1; /* SYMENUM_OPTIONS_DEFAULT */
  const DbgHelpSymEnumCallback callback = &sym_dbg_enum_proc;

  SymDbgEnumCtx ctx = {
      .dbg = dbg,
      .reg = reg,
  };
  if (dbg->SymEnumSymbolsEx(dbg->process, dbg->dbgHelpBaseAddr, "*", callback, &ctx, options)) {
    return true;
  }
  return false;
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

void symbol_pal_dbg_init(SymbolReg* reg) {
  Allocator* bumpAlloc = alloc_bump_create_stack(4 * usize_kibibyte);

  SymDbg dbg = {.process = GetCurrentProcess()};
  if (!sym_dbg_lib_load(&dbg, bumpAlloc)) {
    goto Done;
  }
  if (!sym_dbg_lib_begin(&dbg)) {
    goto Done;
  }
  symbol_reg_set_offset(reg, dbg.dbgHelpBaseAddr);
  if (!sym_dbg_query(&dbg, reg)) {
    goto Done;
  }

Done:
  if (dbg.dbgHelpActive) {
    sym_dbg_lib_end(&dbg);
  }
  if (dbg.dbgHelp) {
    dynlib_destroy(dbg.dbgHelp);
  }
}
