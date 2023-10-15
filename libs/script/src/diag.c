#include "core_alloc.h"
#include "core_diag.h"
#include "core_format.h"
#include "script_diag.h"

struct sScriptDiagBag {
  Allocator* alloc;
  u32        count;
  ScriptDiag values[script_diag_max];
};

ScriptDiagBag* script_diag_bag_create(Allocator* alloc) {
  ScriptDiagBag* bag = alloc_alloc_t(alloc, ScriptDiagBag);
  *bag               = (ScriptDiagBag){.alloc = alloc};
  return bag;
}

void script_diag_bag_destroy(ScriptDiagBag* bag) { alloc_free_t(bag->alloc, bag); }

const ScriptDiag* script_diag_data(const ScriptDiagBag* bag) { return bag->values; }

u32 script_diag_count(const ScriptDiagBag* bag) { return bag->count; }

u32 script_diag_count_of_type(const ScriptDiagBag* bag, const ScriptDiagType type) {
  u32 count = 0;
  for (u32 i = 0; i != bag->count; ++i) {
    if (bag->values[i].type == type) {
      ++count;
    }
  }
  return count;
}

const ScriptDiag* script_diag_first_of_type(const ScriptDiagBag* bag, const ScriptDiagType type) {
  for (u32 i = 0; i != bag->count; ++i) {
    if (bag->values[i].type == type) {
      return &bag->values[i];
    }
  }
  return null;
}

bool script_diag_push(ScriptDiagBag* bag, const ScriptDiag* diag) {
  if (UNLIKELY(bag->count == script_diag_max)) {
    return false;
  }
  bag->values[bag->count++] = *diag;
  return true;
}

void script_diag_clear(ScriptDiagBag* bag) { bag->count = 0; }

String script_diag_msg_scratch(const String sourceText, const ScriptDiag* diag) {
  const String rangeText = script_pos_range_text(sourceText, diag->range);

  FormatArg formatArgs[2] = {0};
  if (rangeText.size < 32) {
    formatArgs[0] = fmt_text(rangeText, .flags = FormatTextFlags_EscapeNonPrintAscii);
  }
  return format_write_formatted_scratch(script_error_str(diag->error), formatArgs);
}

void script_diag_pretty_write(DynString* out, const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptError_None);

  const ScriptPosLineCol rangeStart = script_pos_to_line_col(sourceText, diag->range.start);
  const ScriptPosLineCol rangeEnd   = script_pos_to_line_col(sourceText, diag->range.end);
  fmt_write(
      out,
      "{}:{}-{}:{}: {}",
      fmt_int(rangeStart.line + 1),
      fmt_int(rangeStart.column + 1),
      fmt_int(rangeEnd.line + 1),
      fmt_int(rangeEnd.column + 1),
      fmt_text(script_diag_msg_scratch(sourceText, diag)));
}

String script_diag_pretty_scratch(const String sourceText, const ScriptDiag* diag) {
  diag_assert(diag->error != ScriptError_None);

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_diag_pretty_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
