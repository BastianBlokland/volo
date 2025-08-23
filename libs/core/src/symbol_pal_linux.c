#include "core/alloc.h"
#include "core/bits.h"
#include "core/diag.h"
#include "core/dynlib.h"
#include "core/file.h"
#include "core/forward.h"
#include "core/path.h"

#include "file.h"
#include "symbol.h"

/**
 * To retrieve symbol debug information we parse the DWARF data in the ELF executable.
 * For parsing the DWARF data we rely on 'libdw' which is pre-installed on most linux distributions.
 * NOTE: DWARF info is only available if the executable was build with '-g' and not stripped.
 */

#define elf_ptype_load 1
#define elf_cmd_read 1
#define elf_ev_version 1
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

typedef struct {
  u32  name;
  u32  type;
  u64  flags;
  uptr addr;
  uptr offset;
  u64  size;
  u32  link;
  u32  info;
  u64  addralign;
  u64  entsize;
} ElfSHeader;

typedef struct {
  void* buf;
  i32   type;
  u32   version;
  usize size;
  i64   off;
  usize align;
} ElfData;

typedef struct sElfScn ElfScn;

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
  DynLib* lib;
  Elf*    sessionElf;
  Dwarf*  sessionDwarf;

  // clang-format off
  unsigned int (SYS_DECL* elf_version)(unsigned int);
  Elf*         (SYS_DECL* elf_begin)(int filedes, int elfCmd, Elf* ref /* optional */);
  int          (SYS_DECL* elf_end)(Elf*);
  int          (SYS_DECL* elf_getphdrnum)(Elf*, usize* result);
  ElfScn*      (SYS_DECL* elf_nextscn)(Elf*, ElfScn* curent);
  int          (SYS_DECL* elf_getshdrstrndx)(Elf*, usize* result);
  const char*  (SYS_DECL* elf_strptr)(Elf*, usize index, usize offset);
  ElfData*     (SYS_DECL* elf_getdata)(ElfScn*, ElfData* current);

  ElfPHeader*  (SYS_DECL* gelf_getphdr)(Elf*, int index, ElfPHeader* result);
  ElfSHeader*  (SYS_DECL* gelf_getshdr)(ElfScn*, ElfSHeader* result);

  Dwarf*       (SYS_DECL* dwarf_begin_elf)(Elf*, int cmd, ElfScn* group /* optional */);
  int          (SYS_DECL* dwarf_end)(Dwarf*);
  int          (SYS_DECL* dwarf_nextcu)(Dwarf*, u64 off, u64* nextOff, usize* headerSize, u64* abbrevOffset, u8* addressSize, u8* offsetSize);
  DwarfDie*    (SYS_DECL* dwarf_offdie)(Dwarf*, u64 off, DwarfDie* result);
  int          (SYS_DECL* dwarf_child)(DwarfDie*, DwarfDie* result);
  int          (SYS_DECL* dwarf_lowpc)(DwarfDie*, uptr* result);
  int          (SYS_DECL* dwarf_highpc)(DwarfDie*, uptr* result);
  int          (SYS_DECL* dwarf_siblingof)(DwarfDie*, DwarfDie* result);
  int          (SYS_DECL* dwarf_tag)(DwarfDie*);
  const char*  (SYS_DECL* dwarf_diename)(DwarfDie*);
  // clang-format on
} SymDbg;

static bool sym_dbg_lib_load(SymDbg* dbg, Allocator* alloc) {
  DynLibResult loadRes = dynlib_load(alloc, string_lit("libdw.so.1"), &dbg->lib);
  if (loadRes != DynLibResult_Success) {
    return false;
  }

#define DW_LOAD_SYM(_NAME_)                                                                        \
  do {                                                                                             \
    dbg->_NAME_ = dynlib_symbol(dbg->lib, string_lit(#_NAME_));                                    \
    if (!dbg->_NAME_) {                                                                            \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  DW_LOAD_SYM(elf_version);
  DW_LOAD_SYM(elf_begin);
  DW_LOAD_SYM(elf_end);
  DW_LOAD_SYM(elf_getphdrnum);
  DW_LOAD_SYM(elf_nextscn);
  DW_LOAD_SYM(elf_getshdrstrndx);
  DW_LOAD_SYM(elf_strptr);
  DW_LOAD_SYM(elf_getdata);

  DW_LOAD_SYM(gelf_getphdr);
  DW_LOAD_SYM(gelf_getshdr);

  DW_LOAD_SYM(dwarf_begin_elf);
  DW_LOAD_SYM(dwarf_end);
  DW_LOAD_SYM(dwarf_nextcu);
  DW_LOAD_SYM(dwarf_offdie);
  DW_LOAD_SYM(dwarf_child);
  DW_LOAD_SYM(dwarf_lowpc);
  DW_LOAD_SYM(dwarf_highpc);
  DW_LOAD_SYM(dwarf_siblingof);
  DW_LOAD_SYM(dwarf_tag);
  DW_LOAD_SYM(dwarf_diename);

#undef DW_LOAD_SYM

  if (dbg->elf_version(elf_ev_version) != elf_ev_version) {
    return false; // Unsupported libelf version.
  }

  return true;
}

static bool sym_dbg_elf_begin(SymDbg* dbg, File* file) {
  diag_assert(!dbg->sessionElf);
  dbg->sessionElf = dbg->elf_begin(file->handle, elf_cmd_read, null);
  return dbg->sessionElf != null;
}

static void sym_dbg_elf_end(SymDbg* dbg) {
  diag_assert(dbg->sessionElf);
  dbg->elf_end(dbg->sessionElf);
  dbg->sessionElf = null;
}

static const ElfData* sym_dbg_elf_find_section(SymDbg* dbg, const String name) {
  diag_assert(dbg->sessionElf);
  usize stringTableIndex;
  if (dbg->elf_getshdrstrndx(dbg->sessionElf, &stringTableIndex)) {
    return null;
  }
  for (ElfScn* scn = null; (scn = dbg->elf_nextscn(dbg->sessionElf, scn));) {
    ElfSHeader sectionHeader;
    if (dbg->gelf_getshdr(scn, &sectionHeader) == &sectionHeader) {
      const char* sectionName =
          dbg->elf_strptr(dbg->sessionElf, stringTableIndex, sectionHeader.name);
      if (sectionName && string_eq(string_from_null_term(sectionName), name)) {
        return dbg->elf_getdata(scn, null);
      }
    }
  }
  return null;
}

typedef struct {
  String id;
  u32    checksum; // crc32 (ISO 3309).
} DbgElfDebugLink;

static bool sym_dbg_elf_debuglink(SymDbg* dbg, DbgElfDebugLink* out) {
  diag_assert(dbg->sessionElf);
  const ElfData* section = sym_dbg_elf_find_section(dbg, string_lit(".gnu_debuglink"));
  if (!section || !section->buf) {
    return false; // No debug-link data.
  }
  out->id       = string_from_null_term(section->buf);
  out->checksum = *(u32*)bits_align_ptr(bits_ptr_offset(section->buf, out->id.size + 1), 4);
  return true;
}

/**
 * Find the virtual base address of the elf executable (lowest mapped segment of the executable).
 * NOTE: This does not necessarily match the actual '__executable_start' if address layout
 * randomization is used, when using randomization the ELF base address is usually zero.
 */
static bool sym_dbg_elf_addr_base(SymDbg* dbg, SymbolAddr* out) {
  diag_assert(dbg->sessionElf);
  usize pHeaderCount;
  if (dbg->elf_getphdrnum(dbg->sessionElf, &pHeaderCount)) {
    return false;
  }
  for (usize i = 0; i != pHeaderCount; ++i) {
    ElfPHeader header;
    if (dbg->gelf_getphdr(dbg->sessionElf, (int)i, &header)) {
      if (header.type == elf_ptype_load) {
        *out = header.vaddr; // Use the first loaded segment as the base.
        return true;
      }
    }
  }
  return false;
}

static bool sym_dbg_dwarf_begin(SymDbg* dbg) {
  diag_assert(dbg->sessionElf && !dbg->sessionDwarf);
  dbg->sessionDwarf = dbg->dwarf_begin_elf(dbg->sessionElf, dwarf_cmd_read, null);
  return dbg->sessionDwarf != null;
}

static void sym_dbg_dwarf_end(SymDbg* dbg) {
  dbg->dwarf_end(dbg->sessionDwarf);
  dbg->sessionDwarf = null;
}

static bool sym_dbg_dwarf_query(SymDbg* dbg, const SymbolAddr addrBase, SymbolReg* reg) {
  diag_assert(dbg->sessionDwarf);
  /**
   * Find all the (non-inlined) function symbols in all the compilation units.
   * NOTE: Doesn't depend on 'aranges' dwarf info as that is optional and clang does not emit it.
   */
  u32   foundSymbols = 0;
  u64   cuOffset     = 0;
  u64   cuOffsetNext;
  usize cuSize;
  // Iterate over all compilation units.
  while (!dbg->dwarf_nextcu(dbg->sessionDwarf, cuOffset, &cuOffsetNext, &cuSize, 0, 0, 0)) {
    DwarfDie cu;
    if (!dbg->dwarf_offdie(dbg->sessionDwarf, cuOffset + cuSize, &cu)) {
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
        ++foundSymbols;
      } while (dbg->dwarf_siblingof(&child, &child) == 0);
    }
    cuOffset = cuOffsetNext; // Iterate to the next compilation unit offset.
  }
  return foundSymbols != 0;
}

static bool sym_dbg_file_load(SymDbg* dbg, Allocator* allocTmp, const String path, SymbolReg* reg) {
  bool  result = false;
  File* file   = null;
  if (file_create(allocTmp, path, FileMode_Open, FileAccess_Read, &file)) {
    goto Done;
  }
  if (!sym_dbg_elf_begin(dbg, file)) {
    goto Done;
  }
  SymbolAddr addrBase;
  if (!sym_dbg_elf_addr_base(dbg, &addrBase)) {
    goto Done;
  }
  symbol_reg_set_offset(reg, addrBase);

  DbgElfDebugLink debugLink;
  if (sym_dbg_elf_debuglink(dbg, &debugLink)) {
    /**
     * Debug-link found; debug links are separate elf files that contain the debug symbols (similar
     * to the win32 pdb files).
     * Verify if the debug-link file is present (and matches the checksum); if so use that file
     * instead of the original one.
     */
    const String linkPath = path_build_scratch(path_parent(path), debugLink.id);
    u32          crc;
    if (file_crc_32_path_sync(linkPath, &crc) == FileResult_Success && crc == debugLink.checksum) {
      sym_dbg_elf_end(dbg);

      file_destroy(file);
      file = null;

      if (file_create(allocTmp, linkPath, FileMode_Open, FileAccess_Read, &file)) {
        goto Done;
      }
      if (!sym_dbg_elf_begin(dbg, file)) {
        goto Done;
      }
    }
  }

  if (!sym_dbg_dwarf_begin(dbg)) {
    goto Done;
  }
  if (!sym_dbg_dwarf_query(dbg, addrBase, reg)) {
    goto Done;
  }
  result = true;

Done:
  if (dbg->sessionDwarf) {
    sym_dbg_dwarf_end(dbg);
  }
  if (dbg->sessionElf) {
    sym_dbg_elf_end(dbg);
  }
  if (file) {
    file_destroy(file);
  }
  return result;
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
  if (!sym_dbg_lib_load(&dbg, bumpAlloc)) {
    goto Done;
  }
  if (!sym_dbg_file_load(&dbg, bumpAlloc, g_pathExecutable, reg)) {
    goto Done;
  }
Done:
  if (dbg.lib) {
    dynlib_destroy(dbg.lib);
  }
}
