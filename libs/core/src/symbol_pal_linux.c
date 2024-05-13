#include "core_alloc.h"
#include "core_dynlib.h"
#include "core_file.h"
#include "core_path.h"
#include "core_types.h"

#include "file_internal.h"
#include "symbol_internal.h"

/**
 * To retrieve symbol debug information we parse the DWARF data in the ELF executable.
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

typedef struct {
  File*   exec; // Handle to our own executable.
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
} SymDbg;

static bool sym_dbg_dw_load(SymDbg* dbg, Allocator* alloc) {
  DynLibResult loadRes = dynlib_load(alloc, string_lit("libdw.so.1"), &dbg->dwLib);
  if (loadRes != DynLibResult_Success) {
    return false;
  }

#define DW_LOAD_SYM(_NAME_)                                                                        \
  do {                                                                                             \
    dbg->_NAME_ = dynlib_symbol(dbg->dwLib, string_lit(#_NAME_));                                  \
    if (!dbg->_NAME_) {                                                                            \
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

static bool sym_dbg_dw_begin(SymDbg* dbg) {
  dbg->dwSession = dbg->dwarf_begin(dbg->exec->handle, dwarf_cmd_read);
  return dbg->dwSession != null;
}

static void sym_dbg_dw_end(SymDbg* dbg) { dbg->dwarf_end(dbg->dwSession); }

/**
 * Find the virtual base address of the elf executable (lowest mapped segment of the executable).
 * NOTE: This does not necessarily match the actual '__executable_start' if address layout
 * randomization is used, when using randomization the ELF base address is usually zero.
 */
static bool sym_dbg_addr_base(SymDbg* dbg, SymbolAddr* out) {
  Elf*  elf = dbg->dwarf_getelf(dbg->dwSession);
  usize pHeaderCount;
  if (dbg->elf_getphdrnum(elf, &pHeaderCount)) {
    return false;
  }
  for (usize i = 0; i != pHeaderCount; ++i) {
    ElfPHeader header;
    if (dbg->gelf_getphdr(elf, (int)i, &header)) {
      if (header.type == elf_ptype_load) {
        *out = header.vaddr; // Use the first loaded segment as the base.
        return true;
      }
    }
  }
  return false;
}

static bool sym_dbg_query(SymDbg* dbg, SymbolReg* reg) {
  /**
   * Find all the (non-inlined) function symbols in all the compilation units.
   * NOTE: Doesn't depend on 'aranges' dwarf info as that is optional and clang does not emit it.
   */
  SymbolAddr addrBase;
  if (!sym_dbg_addr_base(dbg, &addrBase)) {
    return false;
  }
  u64    cuOffset = 0;
  u64    cuOffsetNext;
  size_t cuSize;
  // Iterate over all compilation units.
  while (!dbg->dwarf_nextcu(dbg->dwSession, cuOffset, &cuOffsetNext, &cuSize, 0, 0, 0)) {
    DwarfDie cu;
    if (!dbg->dwarf_offdie(dbg->dwSession, cuOffset + cuSize, &cu)) {
      continue;
    }
    // Walk over all the children (functions) in the compilation unit.
    DwarfDie child;
    if (dbg->dwarf_child(&cu, &child) == 0) {
      do {
        const int tag = dbg->dwarf_tag(&child);
        if (tag != dwarf_tag_entrypoint && tag != dwarf_tag_subprogram) {
          continue; // Only (non-inlined) function symbols are supported at this time.
        }
        const char* funcName = dbg->dwarf_diename(&child);
        if (!funcName) {
          continue; // Function is missing a name.
        }
        SymbolAddr addrLow, addrHigh;
        if (dbg->dwarf_lowpc(&child, &addrLow) == -1) {
          continue; // Function is missing an lowpc address.
        }
        if (dbg->dwarf_highpc(&child, &addrHigh) == -1) {
          continue; // Function is missing an highpc address.
        }
        if (addrLow < addrBase || addrHigh < addrLow) {
          continue; // Invalid address, this would mean a corrupt elf file.
        }
        const SymbolAddrRel addrBeginRel = (SymbolAddrRel)(addrLow - addrBase);
        const SymbolAddrRel addrEndRel   = (SymbolAddrRel)(addrHigh - addrBase + 1);
        symbol_reg_add(reg, addrBeginRel, addrEndRel, string_from_null_term(funcName));

      } while (dbg->dwarf_siblingof(&child, &child) == 0);
    }
    cuOffset = cuOffsetNext; // Iterate to the next compilation unit offset.
  }
  return true;
}

SymbolAddr symbol_pal_prog_begin(void) {
  extern const u8 __executable_start[]; // Provided by the linker script.
  return (SymbolAddr)&__executable_start;
}

SymbolAddr symbol_pal_prog_end(void) {
  extern const u8 _etext[]; // Provided by the linker script.
  return (SymbolAddr)&_etext;
}

void symbol_pal_dbg_init(SymbolReg* reg) {
  Allocator* bumpAlloc = alloc_bump_create_stack(4 * usize_kibibyte);

  SymDbg dbg = {0};
  if (file_create(bumpAlloc, g_pathExecutable, FileMode_Open, FileAccess_Read, &dbg.exec)) {
    goto Done;
  }
  if (!sym_dbg_dw_load(&dbg, bumpAlloc)) {
    goto Done;
  }
  if (!sym_dbg_dw_begin(&dbg)) {
    goto Done;
  }
  if (!sym_dbg_query(&dbg, reg)) {
    goto Done;
  }

Done:
  if (dbg.dwSession) {
    sym_dbg_dw_end(&dbg);
  }
  if (dbg.exec) {
    file_destroy(dbg.exec);
  }
  if (dbg.dwLib) {
    dynlib_destroy(dbg.dwLib);
  }
}
