#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "script_sym.h"

ASSERT(script_syms_max < u16_max, "ScriptSymId has to be storable as a 16-bit integer");

INLINE_HINT static bool script_sym_valid(const ScriptSym* sym, const ScriptPos pos) {
  if (sentinel_check(pos)) {
    return true; // 'script_pos_sentinel' indicates that symbols from all ranges should be returned.
  }
  return pos >= sym->validRange.start && pos <= sym->validRange.end;
}

struct sScriptSymBag {
  Allocator* alloc;
  DynArray   symbols; // ScriptSym[]
};

ScriptSymBag* script_sym_bag_create(Allocator* alloc) {
  ScriptSymBag* bag = alloc_alloc_t(alloc, ScriptSymBag);

  *bag = (ScriptSymBag){
      .alloc   = alloc,
      .symbols = dynarray_create_t(alloc, ScriptSym, 128),
  };

  return bag;
}

void script_sym_bag_destroy(ScriptSymBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSym, sym) { string_free(bag->alloc, sym->label); }
  dynarray_destroy(&bag->symbols);
  alloc_free_t(bag->alloc, bag);
}

ScriptSymId script_sym_push(ScriptSymBag* bag, const ScriptSym* sym) {
  diag_assert(!string_is_empty(sym->label));

  const ScriptSymId id = (ScriptSymId)bag->symbols.size;
  if (UNLIKELY(id == script_syms_max)) {
    return script_sym_sentinel;
  }

  *dynarray_push_t(&bag->symbols, ScriptSym) = (ScriptSym){
      .type       = sym->type,
      .label      = string_dup(bag->alloc, sym->label),
      .validRange = sym->validRange,
  };

  return id;
}

void script_sym_clear(ScriptSymBag* bag) {
  dynarray_for_t(&bag->symbols, ScriptSym, sym) { string_free(bag->alloc, sym->label); }
  dynarray_clear(&bag->symbols);
}

String script_sym_type_str(const ScriptSymType type) {
  static const String g_names[] = {
      string_static("Keyword"),
      string_static("BuiltinConstant"),
      string_static("BuiltinFunction"),
      string_static("ExternFunction"),
      string_static("Variable"),
      string_static("MemoryKey"),
  };
  ASSERT(array_elems(g_names) == ScriptSymType_Count, "Incorrect number of ScriptSymType names");

  diag_assert(type < ScriptSymType_Count);
  return g_names[type];
}

const ScriptSym* script_sym_data(const ScriptSymBag* bag, const ScriptSymId id) {
  diag_assert_msg(id < bag->symbols.size, "Invalid symbol-id");
  return dynarray_at_t(&bag->symbols, id, ScriptSym);
}

ScriptSymId script_sym_first(const ScriptSymBag* bag, const ScriptPos pos) {
  if (!bag->symbols.size) {
    return script_sym_sentinel;
  }
  const ScriptSym* first = script_sym_data(bag, 0);
  return script_sym_valid(first, pos) ? 0 : script_sym_next(bag, 0, pos);
}

ScriptSymId script_sym_next(const ScriptSymBag* bag, const ScriptPos pos, ScriptSymId itr) {
  const ScriptSymId lastId = (ScriptSymId)(bag->symbols.size - 1);
  const ScriptSym*  data   = dynarray_begin_t(&bag->symbols, ScriptSym);
  while (itr < lastId) {
    if (script_sym_valid(&data[++itr], pos)) {
      return itr;
    }
  }
  return script_sym_sentinel;
}

void script_sym_write(DynString* out, const String sourceText, const ScriptSym* sym) {
  const ScriptPosLineCol validStart = script_pos_to_line_col(sourceText, sym->validRange.start);
  const ScriptPosLineCol validEnd   = script_pos_to_line_col(sourceText, sym->validRange.end);

  fmt_write(
      out,
      "[{}] {} ({}:{}-{}:{})",
      fmt_text(script_sym_type_str(sym->type)),
      fmt_text(sym->label),
      fmt_int(validStart.line + 1),
      fmt_int(validStart.column + 1),
      fmt_int(validEnd.line + 1),
      fmt_int(validEnd.column + 1));
}

String script_sym_scratch(const String sourceText, const ScriptSym* sym) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_sym_write(&buffer, sourceText, sym);

  return dynstring_view(&buffer);
}
