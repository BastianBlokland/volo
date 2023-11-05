#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_format.h"
#include "script_diag.h"

static const String g_diagKindStrs[] = {
    string_static("Invalid character"),
    string_static("Invalid Utf8 text"),
    string_static("Invalid character in number"),
    string_static("Number ends with a decimal point"),
    string_static("Number ends with a separator"),
    string_static("Key cannot be empty"),
    string_static("String is not terminated"),
    string_static("Recursion limit exceeded"),
    string_static("Variable limit exceeded"),
    string_static("Variable identifier invalid"),
    string_static("Variable identifier '{}' conflicts"),
    string_static("Missing expression"),
    string_static("Invalid expression"),
    string_static("No variable found for identifier '{}'"),
    string_static("No function found for identifier '{}'"),
    string_static("Incorrect argument count for builtin function"),
    string_static("Unclosed parenthesized expression"),
    string_static("Unterminated block"),
    string_static("Unterminated argument list"),
    string_static("Block size exceeds maximum"),
    string_static("Missing semicolon"),
    string_static("Unexpected semicolon"),
    string_static("Unnecessary semicolon"),
    string_static("Argument count exceeds maximum"),
    string_static("Invalid condition count"),
    string_static("Invalid if-expression"),
    string_static("Invalid while-loop"),
    string_static("Invalid for-loop"),
    string_static("Too few for-loop components"),
    string_static("For-loop component is static"),
    string_static("Separator missing in for-loop"),
    string_static("Block expected"),
    string_static("Block or if-expression expected"),
    string_static("Missing colon in select-expression"),
    string_static("Unexpected token after expression"),
    string_static("{} not valid outside a loop body"),
    string_static("Variable declaration is not allowed in this section"),
    string_static("Variable '{}' is not used"),
    string_static("Loops are not allowed in this section"),
    string_static("If-expressions are not allowed in this section"),
    string_static("Return-expressions are not allowed in this section"),
    string_static("Expression has no effect"),
    string_static("Unreachable expressions"),
    string_static("Condition expression is static"),
    string_static("Too few arguments"),
    string_static("Too many arguments"),
};
ASSERT(array_elems(g_diagKindStrs) == ScriptDiagKind_Count, "Incorrect number of kind strs");

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
  return format_write_formatted_scratch(g_diagKindStrs[diag->kind], formatArgs);
}

void script_diag_pretty_write(DynString* out, const String sourceText, const ScriptDiag* diag) {
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
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_diag_pretty_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
