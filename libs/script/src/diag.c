#include "core_alloc.h"
#include "core_diag.h"
#include "core_format.h"
#include "script_diag.h"

struct sScriptDiagBag {
  Allocator*       alloc;
  u32              count;
  ScriptDiagFilter filter;
  ScriptDiag       values[script_diag_max];
};

ScriptDiagBag* script_diag_bag_create(Allocator* alloc, const ScriptDiagFilter filter) {
  ScriptDiagBag* bag = alloc_alloc_t(alloc, ScriptDiagBag);
  *bag               = (ScriptDiagBag){
                    .alloc  = alloc,
                    .filter = filter,
  };
  return bag;
}

void script_diag_bag_destroy(ScriptDiagBag* bag) { alloc_free_t(bag->alloc, bag); }

bool script_diag_active(const ScriptDiagBag* bag, const ScriptDiagSeverity severity) {
  return (bag->filter & (1 << severity)) != 0;
}

const ScriptDiag* script_diag_data(const ScriptDiagBag* bag) { return bag->values; }

u32 script_diag_count(const ScriptDiagBag* bag, const ScriptDiagFilter filter) {
  if (filter == ScriptDiagFilter_All) {
    return bag->count;
  }
  u32 count = 0;
  for (u32 i = 0; i != bag->count; ++i) {
    if (filter & (1 << bag->values[i].severity)) {
      ++count;
    }
  }
  return count;
}

const ScriptDiag* script_diag_first(const ScriptDiagBag* bag, const ScriptDiagFilter filter) {
  for (u32 i = 0; i != bag->count; ++i) {
    if (filter & (1 << bag->values[i].severity)) {
      return &bag->values[i];
    }
  }
  return null;
}

bool script_diag_push(ScriptDiagBag* bag, const ScriptDiag* diag) {
  if (!script_diag_active(bag, diag->severity)) {
    return false;
  }
  if (UNLIKELY(bag->count == script_diag_max)) {
    return false;
  }
  bag->values[bag->count++] = *diag;
  return true;
}

void script_diag_clear(ScriptDiagBag* bag) { bag->count = 0; }

String script_diag_msg_scratch(const String sourceText, const ScriptDiag* diag) {
  const String rangeText = script_range_text(sourceText, diag->range);

  FormatArg formatArgs[2] = {0};
  if (rangeText.size < 32) {
    formatArgs[0] = fmt_text(rangeText);
  }
  return format_write_formatted_scratch(script_error_str(diag->error), formatArgs);
}

void script_diag_pretty_write(DynString* out, const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptError_None);

  const ScriptRangeLineCol rangeLineCol = script_range_to_line_col(sourceText, diag->range);
  fmt_write(
      out,
      "{}:{}-{}:{}: {}",
      fmt_int(rangeLineCol.start.line + 1),
      fmt_int(rangeLineCol.start.column + 1),
      fmt_int(rangeLineCol.end.line + 1),
      fmt_int(rangeLineCol.end.column + 1),
      fmt_text(script_diag_msg_scratch(sourceText, diag)));
}

String script_diag_pretty_scratch(const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptError_None);

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_diag_pretty_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
