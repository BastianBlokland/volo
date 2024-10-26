#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_format.h"
#include "script_diag.h"

// clang-format off

static const String g_diagKindStrs[] = {
    [ScriptDiag_InvalidChar]                      = string_static("Invalid character"),
    [ScriptDiag_InvalidUtf8]                      = string_static("Invalid Utf8 text"),
    [ScriptDiag_InvalidCharInNumber]              = string_static("Invalid character in number"),
    [ScriptDiag_NumberEndsWithDecPoint]           = string_static("Number ends with a decimal point"),
    [ScriptDiag_NumberEndsWithSeparator]          = string_static("Number ends with a separator"),
    [ScriptDiag_KeyEmpty]                         = string_static("Key cannot be empty"),
    [ScriptDiag_UnterminatedString]               = string_static("String is not terminated"),
    [ScriptDiag_UnexpectedWhitespace]             = string_static("Unexpected whitespace"),
    [ScriptDiag_RecursionLimitExceeded]           = string_static("Recursion limit exceeded"),
    [ScriptDiag_VarLimitExceeded]                 = string_static("Variable limit exceeded"),
    [ScriptDiag_VarIdInvalid]                     = string_static("Variable identifier invalid"),
    [ScriptDiag_VarIdConflicts]                   = string_static("Variable identifier '{}' conflicts"),
    [ScriptDiag_MissingPrimaryExpr]               = string_static("Missing expression"),
    [ScriptDiag_InvalidPrimaryExpr]               = string_static("Invalid expression"),
    [ScriptDiag_NoVarFoundForId]                  = string_static("No variable found for identifier '{}'"),
    [ScriptDiag_NoFuncFoundForId]                 = string_static("No function found for identifier '{}'"),
    [ScriptDiag_IncorrectArgCountForBuiltinFunc]  = string_static("Incorrect argument count for builtin function"),
    [ScriptDiag_UnclosedParenthesizedExpr]        = string_static("Unclosed parenthesized expression"),
    [ScriptDiag_UnterminatedBlock]                = string_static("Unterminated block"),
    [ScriptDiag_UnterminatedArgumentList]         = string_static("Unterminated argument list"),
    [ScriptDiag_BlockTooBig]                      = string_static("Block size exceeds maximum"),
    [ScriptDiag_MissingSemicolon]                 = string_static("Missing semicolon"),
    [ScriptDiag_UnexpectedSemicolon]              = string_static("Unexpected semicolon"),
    [ScriptDiag_UnnecessarySemicolon]             = string_static("Unnecessary semicolon"),
    [ScriptDiag_ArgumentCountExceedsMaximum]      = string_static("Argument count exceeds maximum"),
    [ScriptDiag_InvalidConditionCount]            = string_static("Invalid condition count"),
    [ScriptDiag_InvalidIf]                        = string_static("Invalid if-expression"),
    [ScriptDiag_InvalidWhileLoop]                 = string_static("Invalid while-loop"),
    [ScriptDiag_InvalidForLoop]                   = string_static("Invalid for-loop"),
    [ScriptDiag_ForLoopCompMissing]               = string_static("Too few for-loop components"),
    [ScriptDiag_ForLoopCompStatic]                = string_static("For-loop component is static"),
    [ScriptDiag_ForLoopSeparatorMissing]          = string_static("Separator missing in for-loop"),
    [ScriptDiag_BlockExpected]                    = string_static("Block expected"),
    [ScriptDiag_BlockOrIfExpected]                = string_static("Block or if-expression expected"),
    [ScriptDiag_MissingColonInSelectExpr]         = string_static("Missing colon in select-expression"),
    [ScriptDiag_UnexpectedTokenAfterExpr]         = string_static("Unexpected token after expression"),
    [ScriptDiag_OnlyValidInLoop]                  = string_static("{} not valid outside a loop body"),
    [ScriptDiag_VarUnused]                        = string_static("Variable '{}' is not used"),
    [ScriptDiag_ExprHasNoEffect]                  = string_static("Expression has no effect"),
    [ScriptDiag_ExprUnreachable]                  = string_static("Unreachable expressions"),
    [ScriptDiag_ConditionExprStatic]              = string_static("Condition expression is static"),
    [ScriptDiag_TooFewArguments]                  = string_static("Too few arguments"),
    [ScriptDiag_TooManyArguments]                 = string_static("Too many arguments"),
    [ScriptDiag_InvalidArgumentValue]             = string_static("Invalid value for argument"),
};
ASSERT(array_elems(g_diagKindStrs) == ScriptDiagKind_Count, "Incorrect number of kind strs");

// clang-format on

struct sScriptDiagBag {
  Allocator*       alloc;
  u32              count;
  ScriptDiagFilter filter;
  ScriptDiag       values[script_diag_max];
};

ScriptDiagBag* script_diag_bag_create(Allocator* alloc, const ScriptDiagFilter filter) {
  ScriptDiagBag* bag = alloc_alloc_t(alloc, ScriptDiagBag);

  *bag = (ScriptDiagBag){
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
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  script_diag_pretty_write(&buffer, sourceText, diag);

  return dynstring_view(&buffer);
}
