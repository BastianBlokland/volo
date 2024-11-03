#include "app_cli.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_format.h"
#include "core_math.h"
#include "core_sort.h"
#include "core_time.h"
#include "geo_color.h"
#include "json.h"
#include "script_binder.h"
#include "script_eval.h"
#include "script_format.h"
#include "script_lex.h"
#include "script_read.h"
#include "script_sig.h"
#include "script_sym.h"

/**
 * Language Server Protocol implementation for the Volo script language.
 *
 * Specification:
 * https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
 */

#define lsp_script_binders_max 16

typedef enum {
  LspStatus_Running,
  LspStatus_Exit,
  LspStatus_ErrorReadFailed,
  LspStatus_ErrorInvalidJson,
  LspStatus_ErrorInvalidJRpcMessage,
  LspStatus_ErrorUnsupportedJRpcVersion,
  LspStatus_ErrorMalformedNotification,

  LspStatus_Count,
} LspStatus;

static const String g_lspStatusMessage[LspStatus_Count] = {
    [LspStatus_Running]                     = string_static("Running"),
    [LspStatus_Exit]                        = string_static("Exit"),
    [LspStatus_ErrorReadFailed]             = string_static("Error: Read failed"),
    [LspStatus_ErrorInvalidJson]            = string_static("Error: Invalid json received"),
    [LspStatus_ErrorInvalidJRpcMessage]     = string_static("Error: Invalid jrpc message received"),
    [LspStatus_ErrorUnsupportedJRpcVersion] = string_static("Error: Unsupported jrpc version"),
    [LspStatus_ErrorMalformedNotification]  = string_static("Error: Malformed notification"),
};

typedef enum {
  LspFlags_Initialized = 1 << 0,
  LspFlags_Shutdown    = 1 << 1,
  LspFlags_Trace       = 1 << 2,
  LspFlags_Profile     = 1 << 3,
} LspFlags;

typedef struct {
  String         identifier;
  ScriptLookup*  scriptLookup;
  ScriptDoc*     scriptDoc;
  ScriptDiagBag* scriptDiags;
  ScriptSymBag*  scriptSyms;
  ScriptExpr     scriptRoot;
} LspDocument;

typedef struct {
  LspStatus           status;
  LspFlags            flags;
  DynString*          readBuffer;
  usize               readCursor;
  DynString*          writeBuffer;
  const ScriptBinder* scriptBinder;
  JsonDoc*            jDoc;     // Cleared between messages.
  DynArray*           openDocs; // LspDocument[]*
  File*               in;
  File*               out;
  usize               bytesOut; // For diagnostic purposes only.
} LspContext;

typedef struct {
  usize contentLength;
} LspHeader;

typedef enum {
  LspMessageType_Error   = 1,
  LspMessageType_Warning = 2,
  LspMessageType_Info    = 3,
  LspMessageType_Log     = 4,
} LspMessageType;

typedef struct {
  ScriptRangeLineCol range;
  ScriptDiagSeverity severity;
  String             message;
} LspDiag;

typedef struct {
  ScriptRangeLineCol range;
  String             text;
} LspHover;

typedef struct {
  String             uri;
  ScriptRangeLineCol range;
} LspLocation;

typedef enum {
  LspSemanticTokenType_Variable,
  LspSemanticTokenType_Function,
  LspSemanticTokenType_Enum,
} LspSemanticTokenType;

typedef enum {
  LspSemanticTokenMod_None         = 0,
  LspSemanticTokenMod_Definition   = 1 << 0,
  LspSemanticTokenMod_ReadOnly     = 1 << 1,
  LspSemanticTokenMod_Modification = 1 << 2,
} LspSemanticTokenMod;

typedef struct {
  ScriptPosLineCol     pos;
  u16                  length; // In unicode code points.
  LspSemanticTokenType type : 16;
  LspSemanticTokenMod  mod : 16;
} LspSemanticToken;

typedef enum {
  LspCompletionItemKind_Function    = 3,
  LspCompletionItemKind_Constructor = 4,
  LspCompletionItemKind_Variable    = 6,
  LspCompletionItemKind_Property    = 10,
  LspCompletionItemKind_Keyword     = 14,
  LspCompletionItemKind_Constant    = 21,
} LspCompletionItemKind;

typedef struct {
  String                label;
  String                labelDetail;
  String                doc;
  LspCompletionItemKind kind : 8;
  u8                    commitChar;
} LspCompletionItem;

typedef enum {
  LspHighlightKind_Text  = 1,
  LspHighlightKind_Read  = 2,
  LspHighlightKind_Write = 3,
} LspHighlightKind;

typedef struct {
  ScriptRangeLineCol range;
  LspHighlightKind   kind;
} LspHighlight;

typedef struct {
  String           label;
  String           doc;
  const ScriptSig* scriptSig;
} LspSignature;

typedef enum {
  LspSymbolKind_Function = 12,
  LspSymbolKind_Variable = 13,
  LspSymbolKind_Constant = 14,
  LspSymbolKind_Key      = 20,
  LspSymbolKind_Operator = 25,
} LspSymbolKind;

typedef struct {
  String             name;
  ScriptRangeLineCol range;
  LspSymbolKind      kind : 8;
} LspSymbol;

typedef struct {
  ScriptRangeLineCol range;
  String             newText;
} LspTextEdit;

typedef struct {
  LspDocument*     doc;
  ScriptPos        pos;
  ScriptPosLineCol posLc;
} LspTextDocPos;

typedef struct {
  ScriptRangeLineCol range;
  GeoColor           color;
} LspColorInfo;

typedef struct {
  String  method;
  JsonVal params; // Optional, sentinel_u32 if unused.
} JRpcNotification;

typedef struct {
  String  method;
  JsonVal params; // Optional, sentinel_u32 if unused.
  JsonVal id;
} JRpcRequest;

typedef struct {
  i32    code;
  String msg;
} JRpcError;

static const JRpcError g_jrpcErrorMethodNotFound = {
    .code = -32601,
    .msg  = string_static("Method not found"),
};

static const JRpcError g_jrpcErrorInvalidParams = {
    .code = -32602,
    .msg  = string_static("Invalid parameters"),
};

static const JRpcError g_jrpcErrorRenameFailed = {
    .code = -32803,
    .msg  = string_static("Failed to rename symbol"),
};

static const JRpcError g_jrpcErrorInvalidSymbolName = {
    .code = -32803,
    .msg  = string_static("Invalid symbol name"),
};

static i8 lsp_semantic_token_compare(const void* a, const void* b) {
  const LspSemanticToken* tokA = a;
  const LspSemanticToken* tokB = b;

  i8 res = compare_u16(&tokA->pos.line, &tokB->pos.line);
  if (!res) {
    res = compare_u16(&tokA->pos.column, &tokB->pos.column);
  }
  return res;
}

static void lsp_doc_destroy(LspDocument* doc) {
  string_free(g_allocHeap, doc->identifier);
  script_lookup_destroy(doc->scriptLookup);
  script_destroy(doc->scriptDoc);
  script_diag_bag_destroy(doc->scriptDiags);
  script_sym_bag_destroy(doc->scriptSyms);
}

static LspDocument* lsp_doc_find(LspContext* ctx, const String identifier) {
  dynarray_for_t(ctx->openDocs, LspDocument, doc) {
    if (string_eq(doc->identifier, identifier)) {
      return doc;
    }
  }
  return null;
}

static LspDocument* lsp_doc_open(LspContext* ctx, const String identifier, const String text) {
  LspDocument* res = dynarray_push_t(ctx->openDocs, LspDocument);

  *res = (LspDocument){
      .identifier   = string_dup(g_allocHeap, identifier),
      .scriptLookup = script_lookup_create(g_allocHeap),
      .scriptDoc    = script_create(g_allocHeap),
      .scriptDiags  = script_diag_bag_create(g_allocHeap, ScriptDiagFilter_All),
      .scriptSyms   = script_sym_bag_create(g_allocHeap),
  };

  script_lookup_update(res->scriptLookup, text);

  return res;
}

static void lsp_doc_close(LspContext* ctx, LspDocument* doc) {
  lsp_doc_destroy(doc);

  const usize index = doc - dynarray_begin_t(ctx->openDocs, LspDocument);
  dynarray_remove_unordered(ctx->openDocs, index, 1);
}

static LspLocation lsp_doc_location(const LspDocument* doc, const ScriptRange range) {
  return (LspLocation){
      .uri   = doc->identifier,
      .range = script_lookup_range_to_line_col(doc->scriptLookup, range),
  };
}

static String lsp_doc_pos_scratch(const LspTextDocPos* docPos) {
  return fmt_write_scratch(
      "{} [{}:{}]",
      fmt_text(docPos->doc->identifier),
      fmt_int(docPos->posLc.line + 1),
      fmt_int(docPos->posLc.column + 1));
}

static void lsp_read_trim(LspContext* ctx) {
  dynstring_erase_chars(ctx->readBuffer, 0, ctx->readCursor);
  ctx->readCursor = 0;
}

static String lsp_read_remaining(LspContext* ctx) {
  const String full = dynstring_view(ctx->readBuffer);
  return string_slice(full, ctx->readCursor, full.size - ctx->readCursor);
}

static void lsp_read_chunk(LspContext* ctx) {
  const FileResult res = file_read_sync(ctx->in, ctx->readBuffer);
  if (UNLIKELY(res != FileResult_Success)) {
    ctx->status = LspStatus_ErrorReadFailed;
  }
}

static String lsp_read_until(LspContext* ctx, const String pattern) {
  while (LIKELY(ctx->status == LspStatus_Running)) {
    const String text = lsp_read_remaining(ctx);
    const usize  pos  = string_find_first(text, pattern);
    if (!sentinel_check(pos)) {
      ctx->readCursor += pos + pattern.size;
      return string_slice(text, 0, pos + pattern.size);
    }
    lsp_read_chunk(ctx);
  }
  return string_empty;
}

static String lsp_read_sized(LspContext* ctx, const usize size) {
  while (LIKELY(ctx->status == LspStatus_Running)) {
    const String text = lsp_read_remaining(ctx);
    if (text.size >= size) {
      ctx->readCursor += size;
      return string_slice(text, 0, size);
    }
    lsp_read_chunk(ctx);
  }
  return string_empty;
}

static String lsp_header_lex_key(const String input, String* outKey) {
  const usize colonPos = string_find_first(input, string_lit(": "));
  if (sentinel_check(colonPos)) {
    *outKey = string_empty;
    return input;
  }
  *outKey = string_trim_whitespace(string_slice(input, 0, colonPos));
  return string_consume(input, colonPos + 2);
}

static LspHeader lsp_read_header(LspContext* ctx) {
  LspHeader result = {0};
  String    input  = lsp_read_until(ctx, string_lit("\r\n\r\n"));
  while (LIKELY(ctx->status == LspStatus_Running)) {
    String key;
    input = lsp_header_lex_key(input, &key);
    if (string_is_empty(key)) {
      break;
    }
    if (string_eq(key, string_lit("Content-Length"))) {
      input = format_read_u64(input, &result.contentLength, 10);
    }
    input = format_read_line(input, null); // Consume the rest of the line.
  }
  return result;
}

static String lsp_maybe_str(LspContext* ctx, const JsonVal val) {
  if (sentinel_check(val) || json_type(ctx->jDoc, val) != JsonType_String) {
    return string_empty;
  }
  return json_string(ctx->jDoc, val);
}

static f64 lsp_maybe_number(LspContext* ctx, const JsonVal val) {
  if (sentinel_check(val) || json_type(ctx->jDoc, val) != JsonType_Number) {
    return -1.0; // TODO: Should this return NaN instead?
  }
  return json_number(ctx->jDoc, val);
}

static bool lsp_maybe_bool(LspContext* ctx, const JsonVal val) {
  if (sentinel_check(val) || json_type(ctx->jDoc, val) != JsonType_Bool) {
    return false;
  }
  return json_bool(ctx->jDoc, val);
}

static JsonVal lsp_maybe_field(LspContext* ctx, const JsonVal val, const String fieldName) {
  if (sentinel_check(val) || json_type(ctx->jDoc, val) != JsonType_Object) {
    return sentinel_u32;
  }
  return json_field(ctx->jDoc, val, string_hash(fieldName));
}

static JsonVal lsp_position_to_json(LspContext* ctx, const ScriptPosLineCol* pos) {
  const JsonVal obj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, obj, "line", json_add_number(ctx->jDoc, pos->line));
  json_add_field_lit(ctx->jDoc, obj, "character", json_add_number(ctx->jDoc, pos->column));
  return obj;
}

static bool lsp_position_from_json(LspContext* ctx, const JsonVal val, ScriptPosLineCol* out) {
  const JsonVal line = lsp_maybe_field(ctx, val, string_lit("line"));
  if (sentinel_check(line) || json_type(ctx->jDoc, line) != JsonType_Number) {
    return false;
  }
  const JsonVal character = lsp_maybe_field(ctx, val, string_lit("character"));
  if (sentinel_check(character) || json_type(ctx->jDoc, character) != JsonType_Number) {
    return false;
  }
  out->line   = (u16)json_number(ctx->jDoc, line);
  out->column = (u16)json_number(ctx->jDoc, character);
  return true;
}

static JsonVal lsp_range_to_json(LspContext* ctx, const ScriptRangeLineCol* range) {
  const JsonVal obj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, obj, "start", lsp_position_to_json(ctx, &range->start));
  json_add_field_lit(ctx->jDoc, obj, "end", lsp_position_to_json(ctx, &range->end));
  return obj;
}

static bool lsp_range_from_json(LspContext* ctx, const JsonVal val, ScriptRangeLineCol* out) {
  if (!lsp_position_from_json(ctx, lsp_maybe_field(ctx, val, string_lit("start")), &out->start)) {
    return false;
  }
  if (!lsp_position_from_json(ctx, lsp_maybe_field(ctx, val, string_lit("end")), &out->end)) {
    return false;
  }
  return true;
}

static JsonVal lsp_hover_to_json(LspContext* ctx, const LspHover* hover) {
  const JsonVal obj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, obj, "range", lsp_range_to_json(ctx, &hover->range));
  json_add_field_lit(ctx->jDoc, obj, "contents", json_add_string(ctx->jDoc, hover->text));
  return obj;
}

static JsonVal lsp_highlight_to_json(LspContext* ctx, const LspHighlight* highlight) {
  const JsonVal obj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, obj, "range", lsp_range_to_json(ctx, &highlight->range));
  json_add_field_lit(ctx->jDoc, obj, "kind", json_add_number(ctx->jDoc, highlight->kind));
  return obj;
}

static JsonVal lsp_location_to_json(LspContext* ctx, const LspLocation* location) {
  const JsonVal obj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, obj, "uri", json_add_string(ctx->jDoc, location->uri));
  json_add_field_lit(ctx->jDoc, obj, "range", lsp_range_to_json(ctx, &location->range));
  return obj;
}

static JsonVal lsp_selection_range_empty_to_json(LspContext* ctx) {
  const JsonVal obj = json_add_object(ctx->jDoc);
  // TODO: Should this be an empty object or a '0:0 - 0:0' range?
  json_add_field_lit(ctx->jDoc, obj, "range", json_add_object(ctx->jDoc));
  return obj;
}

static JsonVal lsp_selection_range_to_json(
    LspContext* ctx, const ScriptRangeLineCol ranges[], const usize rangeCount) {
  if (!rangeCount) {
    return lsp_selection_range_empty_to_json(ctx);
  }
  const JsonVal headObj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, headObj, "range", lsp_range_to_json(ctx, &ranges[0]));

  JsonVal tailObj = headObj;
  for (u32 i = 1; i != rangeCount; ++i) {
    const JsonVal obj = json_add_object(ctx->jDoc);
    json_add_field_lit(ctx->jDoc, obj, "range", lsp_range_to_json(ctx, &ranges[i]));

    json_add_field_lit(ctx->jDoc, tailObj, "parent", obj);
    tailObj = obj;
  }
  return headObj;
}

/**
 * Pre-condition: Tokens are sorted by position.
 */
static JsonVal lsp_semantic_tokens_to_json(
    LspContext* ctx, const LspSemanticToken tokens[], const usize tokenCount) {

  JsonVal tokensArr = json_add_array(ctx->jDoc);
  for (usize i = 0; i != tokenCount; ++i) {
    const u16 lineNum      = tokens[i].pos.line;
    const u16 lineNumPrev  = i ? tokens[i - 1].pos.line : 0;
    const u16 lineNumDelta = lineNum - lineNumPrev;

    const u16 colNum      = tokens[i].pos.column;
    const u16 colNumPrev  = i ? tokens[i - 1].pos.column : 0;
    const u16 colNumDelta = lineNum == lineNumPrev ? (colNum - colNumPrev) : colNum;

    json_add_elem(ctx->jDoc, tokensArr, json_add_number(ctx->jDoc, lineNumDelta));
    json_add_elem(ctx->jDoc, tokensArr, json_add_number(ctx->jDoc, colNumDelta));
    json_add_elem(ctx->jDoc, tokensArr, json_add_number(ctx->jDoc, tokens[i].length));
    json_add_elem(ctx->jDoc, tokensArr, json_add_number(ctx->jDoc, tokens[i].type));
    json_add_elem(ctx->jDoc, tokensArr, json_add_number(ctx->jDoc, tokens[i].mod));
  }
  return tokensArr;
}

static JsonVal lsp_completion_item_to_json(LspContext* ctx, const LspCompletionItem* item) {
  JsonVal labelDetailsObj = sentinel_u32;
  if (!string_is_empty(item->labelDetail)) {
    labelDetailsObj         = json_add_object(ctx->jDoc);
    const JsonVal detailVal = json_add_string(ctx->jDoc, item->labelDetail);
    json_add_field_lit(ctx->jDoc, labelDetailsObj, "detail", detailVal);
  }

  const JsonVal commitCharsArr = json_add_array(ctx->jDoc);
  if (item->commitChar) {
    const JsonVal commitCharVal = json_add_string(ctx->jDoc, mem_create(&item->commitChar, 1));
    json_add_elem(ctx->jDoc, commitCharsArr, commitCharVal);
  }

  const JsonVal obj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, obj, "label", json_add_string(ctx->jDoc, item->label));
  if (!sentinel_check(labelDetailsObj)) {
    json_add_field_lit(ctx->jDoc, obj, "labelDetails", labelDetailsObj);
  }
  if (!string_is_empty(item->doc)) {
    const JsonVal docMarkupObj = json_add_object(ctx->jDoc);
    json_add_field_lit(ctx->jDoc, docMarkupObj, "value", json_add_string(ctx->jDoc, item->doc));
    json_add_field_lit(ctx->jDoc, docMarkupObj, "kind", json_add_string_lit(ctx->jDoc, "markdown"));

    json_add_field_lit(ctx->jDoc, obj, "documentation", docMarkupObj);
  }
  json_add_field_lit(ctx->jDoc, obj, "kind", json_add_number(ctx->jDoc, item->kind));
  json_add_field_lit(ctx->jDoc, obj, "commitCharacters", commitCharsArr);
  return obj;
}

static JsonVal lsp_symbol_to_json(LspContext* ctx, const LspSymbol* symbol) {
  const JsonVal obj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, obj, "name", json_add_string(ctx->jDoc, symbol->name));
  json_add_field_lit(ctx->jDoc, obj, "kind", json_add_number(ctx->jDoc, symbol->kind));
  json_add_field_lit(ctx->jDoc, obj, "range", lsp_range_to_json(ctx, &symbol->range));
  json_add_field_lit(ctx->jDoc, obj, "selectionRange", lsp_range_to_json(ctx, &symbol->range));
  return obj;
}

static JsonVal lsp_text_edit_to_json(LspContext* ctx, const LspTextEdit* edit) {
  const JsonVal obj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, obj, "range", lsp_range_to_json(ctx, &edit->range));
  json_add_field_lit(ctx->jDoc, obj, "newText", json_add_string(ctx->jDoc, edit->newText));
  return obj;
}

static JsonVal lsp_color_info_to_json(LspContext* ctx, const LspColorInfo* info) {
  const JsonVal colorObj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, colorObj, "red", json_add_number(ctx->jDoc, info->color.r));
  json_add_field_lit(ctx->jDoc, colorObj, "green", json_add_number(ctx->jDoc, info->color.g));
  json_add_field_lit(ctx->jDoc, colorObj, "blue", json_add_number(ctx->jDoc, info->color.b));
  json_add_field_lit(ctx->jDoc, colorObj, "alpha", json_add_number(ctx->jDoc, info->color.a));

  const JsonVal res = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, res, "range", lsp_range_to_json(ctx, &info->range));
  json_add_field_lit(ctx->jDoc, res, "color", colorObj);
  return res;
}

static JsonVal lsp_signature_to_json(LspContext* ctx, const LspSignature* sig) {
  const JsonVal obj = json_add_object(ctx->jDoc);

  const String text =
      fmt_write_scratch("{}{}", fmt_text(sig->label), fmt_text(script_sig_scratch(sig->scriptSig)));
  json_add_field_lit(ctx->jDoc, obj, "label", json_add_string(ctx->jDoc, text));

  if (!string_is_empty(sig->doc)) {
    const JsonVal docMarkupObj = json_add_object(ctx->jDoc);
    json_add_field_lit(ctx->jDoc, docMarkupObj, "value", json_add_string(ctx->jDoc, sig->doc));
    json_add_field_lit(ctx->jDoc, docMarkupObj, "kind", json_add_string_lit(ctx->jDoc, "markdown"));

    json_add_field_lit(ctx->jDoc, obj, "documentation", docMarkupObj);
  }

  const JsonVal paramsArr = json_add_array(ctx->jDoc);
  for (u8 i = 0; i != script_sig_arg_count(sig->scriptSig); ++i) {
    const JsonVal paramObj = json_add_object(ctx->jDoc);

    // TODO: Instead of passing label as a string, pass it as two indices into the signature text.
    const String paramText = script_sig_arg_scratch(sig->scriptSig, i);
    json_add_field_lit(ctx->jDoc, paramObj, "label", json_add_string(ctx->jDoc, paramText));

    json_add_elem(ctx->jDoc, paramsArr, paramObj);
  }
  json_add_field_lit(ctx->jDoc, obj, "parameters", paramsArr);
  return obj;
}

static void lsp_copy_id(LspContext* ctx, const JsonVal obj, const JsonVal id) {
  diag_assert(json_type(ctx->jDoc, obj) == JsonType_Object);
  JsonVal idCopy;
  switch (json_type(ctx->jDoc, id)) {
  case JsonType_Number:
    idCopy = json_add_number(ctx->jDoc, json_number(ctx->jDoc, id));
    break;
  case JsonType_String:
    idCopy = json_add_string(ctx->jDoc, json_string(ctx->jDoc, id));
    break;
  default:
    idCopy = json_add_null(ctx->jDoc);
  }
  json_add_field_lit(ctx->jDoc, obj, "id", idCopy);
}

static LspDocument* lsp_doc_from_json(LspContext* ctx, const JsonVal val) {
  const JsonVal docVal = lsp_maybe_field(ctx, val, string_lit("textDocument"));
  const String  uri    = lsp_maybe_str(ctx, lsp_maybe_field(ctx, docVal, string_lit("uri")));
  if (UNLIKELY(string_is_empty(uri))) {
    return null;
  }
  return lsp_doc_find(ctx, uri);
}

static bool lsp_doc_pos_from_json(LspContext* ctx, const JsonVal val, LspTextDocPos* out) {
  out->doc = lsp_doc_from_json(ctx, val);
  if (UNLIKELY(!out->doc)) {
    return false;
  }
  const JsonVal posLcVal = lsp_maybe_field(ctx, val, string_lit("position"));
  if (UNLIKELY(!lsp_position_from_json(ctx, posLcVal, &out->posLc))) {
    return false;
  }
  out->pos = script_lookup_from_line_col(out->doc->scriptLookup, out->posLc);
  if (UNLIKELY(sentinel_check(out->pos))) {
    return false;
  }
  return true;
}

static LspTextEdit lsp_edit_delta(const String from, const String to) {
  const u32 headMax = (u32)math_min(from.size, to.size);

  // Compute the head index where both strings are still identical.
  u32 head = 0;
  for (; head != headMax; ++head) {
    if (*string_at(from, head) != *string_at(to, head)) {
      break; // Difference found.
    }
  }

  const u32 tailMax = (u32)math_min(to.size - head, from.size - head);

  // Compute the tail index where both strings are still identical.
  u32 tail = 0;
  for (; tail != tailMax; ++tail) {
    if (*string_at(from, from.size - tail - 1) != *string_at(to, to.size - tail - 1)) {
      break; // Difference found.
    }
  }

  const ScriptRange range = {.start = head, .end = (u32)from.size - tail};
  return (LspTextEdit){
      .range   = script_range_to_line_col(from, range),
      .newText = string_slice(to, head, to.size - tail - head),
  };
}

static bool lsp_edit_is_ident(const LspTextEdit* edit) {
  if (edit->range.start.line != edit->range.end.line) {
    return false;
  }
  if (edit->range.start.column != edit->range.end.column) {
    return false;
  }
  return string_is_empty(edit->newText);
}

static void lsp_update_trace_config(LspContext* ctx, const JsonVal traceValue) {
  if (string_eq(lsp_maybe_str(ctx, traceValue), string_lit("off"))) {
    ctx->flags &= ~LspFlags_Trace;
  } else {
    ctx->flags |= LspFlags_Trace;
  }
}

static void lsp_send_json(LspContext* ctx, const JsonVal val) {
  const JsonWriteOpts writeOpts = json_write_opts(.mode = JsonWriteMode_Minimal);
  json_write(ctx->writeBuffer, ctx->jDoc, val, &writeOpts);

  const usize  contentSize = ctx->writeBuffer->size;
  const String headerText  = fmt_write_scratch("Content-Length: {}\r\n\r\n", fmt_int(contentSize));
  dynstring_insert(ctx->writeBuffer, headerText, 0);

  file_write_sync(ctx->out, dynstring_view(ctx->writeBuffer));
  ctx->bytesOut += ctx->writeBuffer->size;
  dynstring_clear(ctx->writeBuffer);
}

static void lsp_send_notification(LspContext* ctx, const JRpcNotification* notif) {
  const JsonVal resp = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, resp, "jsonrpc", json_add_string_lit(ctx->jDoc, "2.0"));
  json_add_field_lit(ctx->jDoc, resp, "method", json_add_string(ctx->jDoc, notif->method));
  if (!sentinel_check(notif->params)) {
    json_add_field_lit(ctx->jDoc, resp, "params", notif->params);
  }
  lsp_send_json(ctx, resp);
}

static void lsp_send_trace(LspContext* ctx, const String message) {
  if (ctx->flags & LspFlags_Trace) {
    const JsonVal params = json_add_object(ctx->jDoc);
    json_add_field_lit(ctx->jDoc, params, "message", json_add_string(ctx->jDoc, message));

    const JRpcNotification notif = {
        .method = string_lit("$/logTrace"),
        .params = params,
    };
    lsp_send_notification(ctx, &notif);
  }
}

static void lsp_send_log(LspContext* ctx, const LspMessageType type, const String message) {
  const JsonVal params = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, params, "type", json_add_number(ctx->jDoc, type));
  json_add_field_lit(ctx->jDoc, params, "message", json_add_string(ctx->jDoc, message));

  const JRpcNotification notif = {
      .method = string_lit("window/logMessage"),
      .params = params,
  };
  lsp_send_notification(ctx, &notif);
}

static void lsp_send_info(LspContext* ctx, const String message) {
  lsp_send_log(ctx, LspMessageType_Info, message);
}

static void lsp_send_error(LspContext* ctx, const String message) {
  lsp_send_log(ctx, LspMessageType_Error, message);
}

static void lsp_send_diagnostics(
    LspContext* ctx, const String docUri, const LspDiag values[], const usize count) {
  const JsonVal diagArray = json_add_array(ctx->jDoc);
  for (u32 i = 0; i != count; ++i) {
    JsonVal severityVal;
    switch (values[i].severity) {
    case ScriptDiagSeverity_Error:
      severityVal = json_add_number(ctx->jDoc, 1);
      break;
    case ScriptDiagSeverity_Warning:
      severityVal = json_add_number(ctx->jDoc, 2);
      break;
    }
    const JsonVal diag = json_add_object(ctx->jDoc);
    json_add_field_lit(ctx->jDoc, diag, "range", lsp_range_to_json(ctx, &values[i].range));
    json_add_field_lit(ctx->jDoc, diag, "severity", severityVal);
    json_add_field_lit(ctx->jDoc, diag, "message", json_add_string(ctx->jDoc, values[i].message));
    json_add_elem(ctx->jDoc, diagArray, diag);
  }

  const JsonVal params = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, params, "uri", json_add_string(ctx->jDoc, docUri));
  json_add_field_lit(ctx->jDoc, params, "diagnostics", diagArray);

  const JRpcNotification notif = {
      .method = string_lit("textDocument/publishDiagnostics"),
      .params = params,
  };
  lsp_send_notification(ctx, &notif);
}

static void lsp_send_response_success(LspContext* ctx, const JRpcRequest* req, const JsonVal val) {
  const JsonVal resp = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, resp, "jsonrpc", json_add_string_lit(ctx->jDoc, "2.0"));
  json_add_field_lit(ctx->jDoc, resp, "result", val);
  lsp_copy_id(ctx, resp, req->id);
  lsp_send_json(ctx, resp);
}

static void lsp_send_response_error(LspContext* ctx, const JRpcRequest* req, const JRpcError* err) {
  const JsonVal errObj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, errObj, "code", json_add_number(ctx->jDoc, err->code));
  json_add_field_lit(ctx->jDoc, errObj, "message", json_add_string(ctx->jDoc, err->msg));

  const JsonVal resp = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, resp, "jsonrpc", json_add_string_lit(ctx->jDoc, "2.0"));
  json_add_field_lit(ctx->jDoc, resp, "error", errObj);
  lsp_copy_id(ctx, resp, req->id);
  lsp_send_json(ctx, resp);
}

static void lsp_analyze_doc(LspContext* ctx, LspDocument* doc) {
  script_clear(doc->scriptDoc);
  script_diag_clear(doc->scriptDiags);
  script_sym_bag_clear(doc->scriptSyms);

  const TimeSteady readStartTime = time_steady_clock();

  doc->scriptRoot = script_read(
      doc->scriptDoc,
      ctx->scriptBinder,
      script_lookup_src(doc->scriptLookup),
      g_stringtable,
      doc->scriptDiags,
      doc->scriptSyms);

  if (ctx->flags & LspFlags_Trace) {
    const TimeDuration dur   = time_steady_duration(readStartTime, time_steady_clock());
    const String       docId = doc->identifier;
    lsp_send_trace(
        ctx, fmt_write_scratch("Document parsed: {} ({})", fmt_text(docId), fmt_duration(dur)));
  }

  LspDiag   lspDiags[script_diag_max];
  const u32 lspDiagCount = script_diag_count(doc->scriptDiags, ScriptDiagFilter_All);
  for (u32 i = 0; i != lspDiagCount; ++i) {
    const ScriptDiag* diag = script_diag_data(doc->scriptDiags) + i;

    // TODO: Report text ranges in utf16 instead of utf32.
    lspDiags[i] = (LspDiag){
        .range    = script_lookup_range_to_line_col(doc->scriptLookup, diag->range),
        .severity = diag->severity,
        .message  = script_diag_msg_scratch(doc->scriptLookup, diag),
    };
  }
  lsp_send_diagnostics(ctx, doc->identifier, lspDiags, lspDiagCount);
}

static void lsp_handle_notif_set_trace(LspContext* ctx, const JRpcNotification* notif) {
  const JsonVal traceVal = lsp_maybe_field(ctx, notif->params, string_lit("value"));
  if (UNLIKELY(sentinel_check(traceVal))) {
    ctx->status = LspStatus_ErrorMalformedNotification;
    return;
  }
  lsp_update_trace_config(ctx, traceVal);
}

static void lsp_handle_notif_exit(LspContext* ctx, const JRpcNotification* notif) {
  (void)notif;
  ctx->status = LspStatus_Exit;
}

static void lsp_handle_notif_initialized(LspContext* ctx, const JRpcNotification* notif) {
  (void)notif;
  ctx->flags |= LspFlags_Initialized;

  lsp_send_info(ctx, string_lit("Server successfully initialized"));
}

static void lsp_handle_notif_doc_did_open(LspContext* ctx, const JRpcNotification* notif) {
  const JsonVal docVal = lsp_maybe_field(ctx, notif->params, string_lit("textDocument"));
  const String  uri    = lsp_maybe_str(ctx, lsp_maybe_field(ctx, docVal, string_lit("uri")));
  if (UNLIKELY(string_is_empty(uri))) {
    ctx->status = LspStatus_ErrorMalformedNotification;
    return;
  }
  const String text = lsp_maybe_str(ctx, lsp_maybe_field(ctx, docVal, string_lit("text")));

  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Document open: {}", fmt_text(uri)));
  }

  if (UNLIKELY(lsp_doc_find(ctx, uri))) {
    lsp_send_error(ctx, fmt_write_scratch("Document already open: {}", fmt_text(uri)));
    return;
  }
  LspDocument* doc = lsp_doc_open(ctx, uri, text);
  lsp_analyze_doc(ctx, doc);

  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Document count: {}", fmt_int(ctx->openDocs->size)));
  }
}

static void lsp_handle_notif_doc_did_close(LspContext* ctx, const JRpcNotification* notif) {
  LspDocument* doc = lsp_doc_from_json(ctx, notif->params);
  if (LIKELY(doc)) {
    if (ctx->flags & LspFlags_Trace) {
      lsp_send_trace(ctx, fmt_write_scratch("Document close: {}", fmt_text(doc->identifier)));
    }
    lsp_send_diagnostics(ctx, doc->identifier, null, 0);
    lsp_doc_close(ctx, doc);
  } else {
    lsp_send_error(ctx, fmt_write_scratch("Document not open: {}", fmt_text(doc->identifier)));
  }
  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Document count: {}", fmt_int(ctx->openDocs->size)));
  }
}

static void lsp_handle_notif_doc_did_change(LspContext* ctx, const JRpcNotification* notif) {
  LspDocument* doc = lsp_doc_from_json(ctx, notif->params);
  if (UNLIKELY(!doc)) {
    lsp_send_error(ctx, fmt_write_scratch("Document not open: {}", fmt_text(doc->identifier)));
    return;
  }

  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Document update: {}", fmt_text(doc->identifier)));
  }

  const JsonVal changesArr = lsp_maybe_field(ctx, notif->params, string_lit("contentChanges"));
  if (sentinel_check(changesArr) || json_type(ctx->jDoc, changesArr) != JsonType_Array) {
    goto InvalidChange;
  }

  // Apply the changes.
  json_for_elems(ctx->jDoc, changesArr, change) {
    const String  newText  = lsp_maybe_str(ctx, lsp_maybe_field(ctx, change, string_lit("text")));
    const JsonVal rangeVal = lsp_maybe_field(ctx, change, string_lit("range"));
    if (sentinel_check(rangeVal)) {
      script_lookup_update(doc->scriptLookup, newText); // No range provided; replace all text.
    } else {
      ScriptRangeLineCol rangeLc;
      if (!lsp_range_from_json(ctx, rangeVal, &rangeLc)) {
        goto InvalidChange;
      }
      const ScriptRange range = script_lookup_range_from_line_col(doc->scriptLookup, rangeLc);
      if (!script_range_valid(range)) {
        goto InvalidChange;
      }
      script_lookup_update_range(doc->scriptLookup, newText, range);
    }
  }

  // Re-analyze the document.
  lsp_analyze_doc(ctx, doc);
  return;

InvalidChange:
  lsp_send_error(ctx, fmt_write_scratch("Invalid document change notification"));
}

static void lsp_handle_notif(LspContext* ctx, const JRpcNotification* notif) {
  static const struct {
    String method;
    void (*handler)(LspContext*, const JRpcNotification*);
  } g_handlers[] = {
      {string_static("$/setTrace"), lsp_handle_notif_set_trace},
      {string_static("exit"), lsp_handle_notif_exit},
      {string_static("initialized"), lsp_handle_notif_initialized},
      {string_static("textDocument/didOpen"), lsp_handle_notif_doc_did_open},
      {string_static("textDocument/didClose"), lsp_handle_notif_doc_did_close},
      {string_static("textDocument/didChange"), lsp_handle_notif_doc_did_change},
  };

  for (u32 i = 0; i != array_elems(g_handlers); ++i) {
    if (string_eq(notif->method, g_handlers[i].method)) {
      g_handlers[i].handler(ctx, notif);
      return;
    }
  }

  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Unhandled notification: {}", fmt_text(notif->method)));
  }
}

static JsonVal lsp_semantic_tokens_legend(LspContext* ctx) {
  const JsonVal tokenTypesArr = json_add_array(ctx->jDoc);
  json_add_elem(ctx->jDoc, tokenTypesArr, json_add_string_lit(ctx->jDoc, "variable"));
  json_add_elem(ctx->jDoc, tokenTypesArr, json_add_string_lit(ctx->jDoc, "function"));
  json_add_elem(ctx->jDoc, tokenTypesArr, json_add_string_lit(ctx->jDoc, "enum"));

  const JsonVal tokenModifiersArr = json_add_array(ctx->jDoc);
  json_add_elem(ctx->jDoc, tokenModifiersArr, json_add_string_lit(ctx->jDoc, "definition"));
  json_add_elem(ctx->jDoc, tokenModifiersArr, json_add_string_lit(ctx->jDoc, "readonly"));
  json_add_elem(ctx->jDoc, tokenModifiersArr, json_add_string_lit(ctx->jDoc, "modification"));

  const JsonVal legendObj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, legendObj, "tokenTypes", tokenTypesArr);
  json_add_field_lit(ctx->jDoc, legendObj, "tokenModifiers", tokenModifiersArr);
  return legendObj;
}

static void lsp_handle_req_initialize(LspContext* ctx, const JRpcRequest* req) {
  const JsonVal traceVal = lsp_maybe_field(ctx, req->params, string_lit("trace"));
  if (!sentinel_check(traceVal)) {
    lsp_update_trace_config(ctx, traceVal);
  }

  const JsonVal options = lsp_maybe_field(ctx, req->params, string_lit("initializationOptions"));
  const JsonVal optionProfile = lsp_maybe_field(ctx, options, string_lit("profile"));
  if (lsp_maybe_bool(ctx, optionProfile)) {
    ctx->flags |= LspFlags_Profile;
  }

  const JsonVal docSyncOpts = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, docSyncOpts, "openClose", json_add_bool(ctx->jDoc, true));
  json_add_field_lit(ctx->jDoc, docSyncOpts, "change", json_add_number(ctx->jDoc, 2));

  const JsonVal hoverOpts      = json_add_object(ctx->jDoc);
  const JsonVal definitionOpts = json_add_object(ctx->jDoc);

  const JsonVal completionTriggerCharArr = json_add_array(ctx->jDoc);
  json_add_elem(ctx->jDoc, completionTriggerCharArr, json_add_string_lit(ctx->jDoc, "$"));

  const JsonVal completionOpts = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, completionOpts, "resolveProvider", json_add_bool(ctx->jDoc, false));
  json_add_field_lit(ctx->jDoc, completionOpts, "triggerCharacters", completionTriggerCharArr);

  const JsonVal signatureTriggerCharArr = json_add_array(ctx->jDoc);
  json_add_elem(ctx->jDoc, signatureTriggerCharArr, json_add_string_lit(ctx->jDoc, "("));
  json_add_elem(ctx->jDoc, signatureTriggerCharArr, json_add_string_lit(ctx->jDoc, ","));

  const JsonVal signatureHelpOpts = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, signatureHelpOpts, "triggerCharacters", signatureTriggerCharArr);

  const JsonVal semanticTokensOpts = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, semanticTokensOpts, "legend", lsp_semantic_tokens_legend(ctx));
  json_add_field_lit(ctx->jDoc, semanticTokensOpts, "full", json_add_bool(ctx->jDoc, true));

  const JsonVal colorOpts          = json_add_object(ctx->jDoc);
  const JsonVal formattingOpts     = json_add_object(ctx->jDoc);
  const JsonVal highlightOpts      = json_add_object(ctx->jDoc);
  const JsonVal referencesOpts     = json_add_object(ctx->jDoc);
  const JsonVal renameOpts         = json_add_object(ctx->jDoc);
  const JsonVal selectionRangeOpts = json_add_object(ctx->jDoc);
  const JsonVal symbolOpts         = json_add_object(ctx->jDoc);

  const JsonVal capabilities = json_add_object(ctx->jDoc);
  // NOTE: At the time of writing VSCode only supports utf-16 position encoding.
  const JsonVal positionEncoding = json_add_string_lit(ctx->jDoc, "utf-16");
  json_add_field_lit(ctx->jDoc, capabilities, "colorProvider", colorOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "completionProvider", completionOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "definitionProvider", definitionOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "documentFormattingProvider", formattingOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "documentHighlightProvider", highlightOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "documentSymbolProvider", symbolOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "hoverProvider", hoverOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "positionEncoding", positionEncoding);
  json_add_field_lit(ctx->jDoc, capabilities, "referencesProvider", referencesOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "renameProvider", renameOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "selectionRangeProvider", selectionRangeOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "semanticTokensProvider", semanticTokensOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "signatureHelpProvider", signatureHelpOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "textDocumentSync", docSyncOpts);

  const JsonVal info          = json_add_object(ctx->jDoc);
  const JsonVal serverName    = json_add_string_lit(ctx->jDoc, "Volo Language Server");
  const JsonVal serverVersion = json_add_string_lit(ctx->jDoc, "0.1");
  json_add_field_lit(ctx->jDoc, info, "name", serverName);
  json_add_field_lit(ctx->jDoc, info, "version", serverVersion);

  const JsonVal result = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, result, "capabilities", capabilities);
  json_add_field_lit(ctx->jDoc, result, "serverInfo", info);

  lsp_send_response_success(ctx, req, result);
}

static void lsp_handle_req_shutdown(LspContext* ctx, const JRpcRequest* req) {
  ctx->flags |= LspFlags_Shutdown;
  lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
}

static void lsp_handle_req_color_representation(LspContext* ctx, const JRpcRequest* req) {
  const LspDocument* doc = lsp_doc_from_json(ctx, req->params);
  if (UNLIKELY(!doc)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  const JsonVal colObj = lsp_maybe_field(ctx, req->params, string_lit("color"));
  const f64     colR   = lsp_maybe_number(ctx, lsp_maybe_field(ctx, colObj, string_lit("red")));
  const f64     colG   = lsp_maybe_number(ctx, lsp_maybe_field(ctx, colObj, string_lit("green")));
  const f64     colB   = lsp_maybe_number(ctx, lsp_maybe_field(ctx, colObj, string_lit("blue")));
  const f64     colA   = lsp_maybe_number(ctx, lsp_maybe_field(ctx, colObj, string_lit("alpha")));

  const String constructLabel = fmt_write_scratch(
      "color({}, {}, {}, {})",
      fmt_float(colR, .minDecDigits = 2, .maxDecDigits = 2),
      fmt_float(colG, .minDecDigits = 2, .maxDecDigits = 2),
      fmt_float(colB, .minDecDigits = 2, .maxDecDigits = 2),
      fmt_float(colA, .minDecDigits = 2, .maxDecDigits = 2));

  const JsonVal constructObj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, constructObj, "label", json_add_string(ctx->jDoc, constructLabel));

  const JsonVal resultArr = json_add_array(ctx->jDoc);
  json_add_elem(ctx->jDoc, resultArr, constructObj);
  lsp_send_response_success(ctx, req, resultArr);
}

static LspCompletionItemKind lsp_completion_kind_for_sym(const ScriptSymKind symKind) {
  switch (symKind) {
  case ScriptSymKind_Keyword:
    return LspCompletionItemKind_Keyword;
  case ScriptSymKind_BuiltinConstant:
    return LspCompletionItemKind_Constant;
  case ScriptSymKind_BuiltinFunction:
    // NOTE: This is taking some creative liberties with the 'Constructor' meaning.
    return LspCompletionItemKind_Constructor;
  case ScriptSymKind_ExternFunction:
    return LspCompletionItemKind_Function;
  case ScriptSymKind_Variable:
    return LspCompletionItemKind_Variable;
  case ScriptSymKind_MemoryKey:
    return LspCompletionItemKind_Property;
  case ScriptSymKind_Count:
    break;
  }
  diag_crash();
}

static void lsp_handle_req_completion(LspContext* ctx, const JRpcRequest* req) {
  LspTextDocPos docPos;
  if (UNLIKELY(!lsp_doc_pos_from_json(ctx, req->params, &docPos))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  if (ctx->flags & LspFlags_Trace) {
    const String txt = fmt_write_scratch("Complete: {}", fmt_text(lsp_doc_pos_scratch(&docPos)));
    lsp_send_trace(ctx, txt);
  }

  const ScriptSymBag* scriptSyms   = docPos.doc->scriptSyms;
  const String        scriptSource = script_lookup_src(docPos.doc->scriptLookup);

  // NOTE: The cursor can be after the last character, in which case its outside of the document
  // text (and we won't find any completion items), to counter this we clamp it.
  const ScriptPos pos = math_min(docPos.pos, (u32)scriptSource.size - 1);

  const JsonVal itemsArr = json_add_array(ctx->jDoc);

  ScriptSym itr = script_sym_first(scriptSyms, pos);
  for (; !sentinel_check(itr); itr = script_sym_next(scriptSyms, pos, itr)) {
    const ScriptSymKind     kind           = script_sym_kind(scriptSyms, itr);
    const ScriptSig*        sig            = script_sym_sig(scriptSyms, itr);
    const LspCompletionItem completionItem = {
        .label       = script_sym_label(scriptSyms, itr),
        .labelDetail = sig ? script_sig_scratch(sig) : string_empty,
        .doc         = script_sym_doc(scriptSyms, itr),
        .kind        = lsp_completion_kind_for_sym(kind),
        .commitChar  = script_sym_is_func(scriptSyms, itr) ? '(' : ' ',
    };
    json_add_elem(ctx->jDoc, itemsArr, lsp_completion_item_to_json(ctx, &completionItem));
  }
  lsp_send_response_success(ctx, req, itemsArr);
}

static void lsp_handle_req_definition(LspContext* ctx, const JRpcRequest* req) {
  LspTextDocPos docPos;
  if (UNLIKELY(!lsp_doc_pos_from_json(ctx, req->params, &docPos))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  if (ctx->flags & LspFlags_Trace) {
    const String txt = fmt_write_scratch("Goto: {}", fmt_text(lsp_doc_pos_scratch(&docPos)));
    lsp_send_trace(ctx, txt);
  }

  const ScriptDoc*    scriptDoc  = docPos.doc->scriptDoc;
  const ScriptSymBag* scriptSyms = docPos.doc->scriptSyms;
  const ScriptExpr    scriptRoot = docPos.doc->scriptRoot;

  if (sentinel_check(scriptRoot)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // Script did not parse correctly (likely due to structural errors).
  }

  const ScriptExpr refExpr = script_expr_find(scriptDoc, scriptRoot, docPos.pos, null, null);
  const ScriptSym  sym     = script_sym_find(scriptSyms, scriptDoc, refExpr);
  if (sentinel_check(sym)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // No symbol found for the expression.
  }

  const ScriptRange symRange = script_sym_location(scriptSyms, sym);
  if (!script_range_valid(symRange)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // No location found for the symbol.
  }

  const LspLocation location = lsp_doc_location(docPos.doc, symRange);
  lsp_send_response_success(ctx, req, lsp_location_to_json(ctx, &location));
}

typedef struct {
  struct {
    ScriptRange range;
    GeoColor    color;
  } entries[32];
  u32 entryCount;
} LspColorContext;

static bool lsp_expr_potential_color(const ScriptDoc* doc, const ScriptExpr expr) {
  const ScriptExprKind exprKind = script_expr_kind(doc, expr);
  if (exprKind == ScriptExprKind_Value) {
    return true;
  }
  if (script_expr_is_intrinsic(doc, expr, ScriptIntrinsic_ColorCompose)) {
    return true;
  }
  if (script_expr_is_intrinsic(doc, expr, ScriptIntrinsic_ColorComposeHsv)) {
    return true;
  }
  return false;
}

static void lsp_collect_colors(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  LspColorContext* colorCtx = ctx;
  if (colorCtx->entryCount == array_elems(colorCtx->entries)) {
    return; // Maximum amount of colors found.
  }
  if (lsp_expr_potential_color(doc, expr) && script_expr_static(doc, expr)) {
    const ScriptVal val = script_expr_static_val(doc, expr);
    if (script_type(val) == ScriptType_Color) {
      colorCtx->entries[colorCtx->entryCount].range = script_expr_range(doc, expr);
      colorCtx->entries[colorCtx->entryCount].color = script_get_color(val, geo_color_white);
      ++colorCtx->entryCount;
    }
  }
}

static void lsp_handle_req_color(LspContext* ctx, const JRpcRequest* req) {
  const LspDocument* doc = lsp_doc_from_json(ctx, req->params);
  if (UNLIKELY(!doc)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  const ScriptDoc* scriptDoc  = doc->scriptDoc;
  const ScriptExpr scriptRoot = doc->scriptRoot;

  LspColorContext colorCtx = {0};
  if (!sentinel_check(scriptRoot)) {
    script_expr_visit(scriptDoc, scriptRoot, &colorCtx, lsp_collect_colors);
  }

  const JsonVal resultArr = json_add_array(ctx->jDoc);
  for (u32 i = 0; i != colorCtx.entryCount; ++i) {
    const LspColorInfo info = {
        .range = script_lookup_range_to_line_col(doc->scriptLookup, colorCtx.entries[i].range),
        .color = colorCtx.entries[i].color,
    };
    json_add_elem(ctx->jDoc, resultArr, lsp_color_info_to_json(ctx, &info));
  }
  lsp_send_response_success(ctx, req, resultArr);
}

static LspHighlightKind lsp_sym_ref_highlight_kind(const ScriptSymRef* ref) {
  switch (ref->kind) {
  case ScriptSymRefKind_Write:
    return LspHighlightKind_Write;
  case ScriptSymRefKind_Read:
  case ScriptSymRefKind_Call:
    return LspHighlightKind_Read;
  }
  diag_crash_msg("Unsupported reference");
}

static void lsp_handle_req_highlight(LspContext* ctx, const JRpcRequest* req) {
  LspTextDocPos docPos;
  if (UNLIKELY(!lsp_doc_pos_from_json(ctx, req->params, &docPos))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  if (ctx->flags & LspFlags_Trace) {
    const String txt = fmt_write_scratch("Highlight: {}", fmt_text(lsp_doc_pos_scratch(&docPos)));
    lsp_send_trace(ctx, txt);
  }

  const ScriptDoc*    scriptDoc  = docPos.doc->scriptDoc;
  const ScriptSymBag* scriptSyms = docPos.doc->scriptSyms;
  const ScriptExpr    scriptRoot = docPos.doc->scriptRoot;

  if (sentinel_check(scriptRoot)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // Script did not parse correctly (likely due to structural errors).
  }

  const ScriptExpr refExpr = script_expr_find(scriptDoc, scriptRoot, docPos.pos, null, null);
  if (sentinel_check(refExpr)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // No expression found at the given position.
  }
  const ScriptSym sym = script_sym_find(scriptSyms, scriptDoc, refExpr);
  if (sentinel_check(sym)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // No symbol found for the expression.
  }

  const JsonVal highlightsArr = json_add_array(ctx->jDoc);

  // Highlight the symbol declaration.
  const ScriptRange symRange = script_sym_location(scriptSyms, sym);
  if (script_range_valid(symRange)) {
    const LspHighlight highlight = {
        .range = script_lookup_range_to_line_col(docPos.doc->scriptLookup, symRange),
        .kind  = LspHighlightKind_Write,
    };
    json_add_elem(ctx->jDoc, highlightsArr, lsp_highlight_to_json(ctx, &highlight));
  }

  // Highlight the symbol references.
  const ScriptSymRefSet refs = script_sym_refs(scriptSyms, sym);
  for (const ScriptSymRef* ref = refs.begin; ref != refs.end; ++ref) {
    const LspHighlight highlight = {
        .range = script_lookup_range_to_line_col(docPos.doc->scriptLookup, ref->location),
        .kind  = lsp_sym_ref_highlight_kind(ref),
    };
    json_add_elem(ctx->jDoc, highlightsArr, lsp_highlight_to_json(ctx, &highlight));
  }
  lsp_send_response_success(ctx, req, highlightsArr);
}

static LspSymbolKind lsp_sym_kind_map(const ScriptSymKind symKind) {
  switch (symKind) {
  case ScriptSymKind_Keyword:
    return LspSymbolKind_Operator;
  case ScriptSymKind_BuiltinConstant:
    return LspSymbolKind_Constant;
  case ScriptSymKind_BuiltinFunction:
  case ScriptSymKind_ExternFunction:
    return LspSymbolKind_Function;
  case ScriptSymKind_Variable:
    return LspSymbolKind_Variable;
  case ScriptSymKind_MemoryKey:
    return LspSymbolKind_Key;
  case ScriptSymKind_Count:
    break;
  }
  diag_crash_msg("Unsupported symbol kind");
}

static void lsp_handle_req_symbols(LspContext* ctx, const JRpcRequest* req) {
  const LspDocument* doc = lsp_doc_from_json(ctx, req->params);
  if (UNLIKELY(!doc)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }
  const JsonVal symbolsArr = json_add_array(ctx->jDoc);

  ScriptSym itr = script_sym_first(doc->scriptSyms, script_pos_sentinel);
  for (; !sentinel_check(itr); itr = script_sym_next(doc->scriptSyms, script_pos_sentinel, itr)) {
    const ScriptSymKind kind     = script_sym_kind(doc->scriptSyms, itr);
    const ScriptRange   location = script_sym_location(doc->scriptSyms, itr);
    if (!script_range_valid(location)) {
      continue; // Symbol has no location.
    }
    // TODO: Report text ranges in utf16 instead of utf32.
    const LspSymbol symbol = {
        .name  = script_sym_label(doc->scriptSyms, itr),
        .kind  = lsp_sym_kind_map(kind),
        .range = script_lookup_range_to_line_col(doc->scriptLookup, location),
    };
    json_add_elem(ctx->jDoc, symbolsArr, lsp_symbol_to_json(ctx, &symbol));
  }
  lsp_send_response_success(ctx, req, symbolsArr);
}

static void lsp_handle_req_formatting(LspContext* ctx, const JRpcRequest* req) {
  const LspDocument* doc = lsp_doc_from_json(ctx, req->params);
  if (UNLIKELY(!doc)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }
  const JsonVal optsVal = lsp_maybe_field(ctx, req->params, string_lit("options"));
  if (sentinel_check(optsVal)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }
  const f64 tabSize = lsp_maybe_number(ctx, lsp_maybe_field(ctx, optsVal, string_lit("tabSize")));
  if (UNLIKELY(tabSize < 1.0 || tabSize > 8.0)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  const String sourceText         = script_lookup_src(doc->scriptLookup);
  const usize  expectedResultSize = (usize)(sourceText.size * 1.5f); // Guesstimate the output size.
  DynString    resultBuffer       = dynstring_create(g_allocHeap, expectedResultSize);

  const TimeSteady formatStartTime = time_steady_clock();

  const ScriptFormatSettings settings = {.indentSize = (u32)tabSize};
  script_format(&resultBuffer, sourceText, &settings);

  if (ctx->flags & LspFlags_Trace) {
    const TimeDuration dur   = time_steady_duration(formatStartTime, time_steady_clock());
    const String       docId = doc->identifier;
    lsp_send_trace(
        ctx, fmt_write_scratch("Document formatted: {} ({})", fmt_text(docId), fmt_duration(dur)));
  }

  const String  formattedDocText = dynstring_view(&resultBuffer);
  const JsonVal editsArr         = json_add_array(ctx->jDoc);

  // TODO: Report text ranges in utf16 instead of utf32.
  const LspTextEdit edit = lsp_edit_delta(sourceText, formattedDocText);
  if (lsp_edit_is_ident(&edit)) {
    diag_assert(string_eq(sourceText, formattedDocText));
  } else {
    diag_assert(!string_eq(sourceText, formattedDocText));
    json_add_elem(ctx->jDoc, editsArr, lsp_text_edit_to_json(ctx, &edit));
  }
  lsp_send_response_success(ctx, req, editsArr);

  dynarray_destroy(&resultBuffer);
}

static void lsp_handle_req_hover(LspContext* ctx, const JRpcRequest* req) {
  LspTextDocPos docPos;
  if (UNLIKELY(!lsp_doc_pos_from_json(ctx, req->params, &docPos))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  if (ctx->flags & LspFlags_Trace) {
    const String txt = fmt_write_scratch("Hover: {}", fmt_text(lsp_doc_pos_scratch(&docPos)));
    lsp_send_trace(ctx, txt);
  }

  const ScriptDoc*    scriptDoc    = docPos.doc->scriptDoc;
  const ScriptSymBag* scriptSyms   = docPos.doc->scriptSyms;
  const ScriptLookup* scriptLookup = docPos.doc->scriptLookup;
  const ScriptExpr    scriptRoot   = docPos.doc->scriptRoot;

  if (sentinel_check(scriptRoot)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // Script did not parse correctly (likely due to structural errors); no hover possible.
  }

  const ScriptExpr     expr     = script_expr_find(scriptDoc, scriptRoot, docPos.pos, null, null);
  const ScriptExprKind exprKind = script_expr_kind(scriptDoc, expr);

  if (exprKind == ScriptExprKind_Block) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // Ignore hovers on block expressions.
  }

  DynString textBuffer = dynstring_create(g_allocScratch, usize_kibibyte);
  dynstring_append(&textBuffer, script_expr_kind_str(exprKind));

  if (script_expr_static(scriptDoc, expr)) {
    const ScriptEvalResult evalRes = script_eval(scriptDoc, scriptLookup, expr, null, null, null);
    fmt_write(&textBuffer, " `{}`", fmt_text(script_val_scratch(evalRes.val)));
  }
  const ScriptSym sym = script_sym_find(scriptSyms, scriptDoc, expr);
  if (!sentinel_check(sym)) {
    const String     label = script_sym_label(scriptSyms, sym);
    const ScriptSig* sig   = script_sym_sig(scriptSyms, sym);
    if (sig) {
      fmt_write(&textBuffer, "\n\n`{}{}`", fmt_text(label), fmt_text(script_sig_scratch(sig)));
    }
    const String documentation = script_sym_doc(scriptSyms, sym);
    if (!string_is_empty(documentation)) {
      fmt_write(&textBuffer, "\n\n{}", fmt_text(documentation));
    }
  }

  const LspHover hover = {
      .range = script_lookup_range_to_line_col(scriptLookup, script_expr_range(scriptDoc, expr)),
      .text  = dynstring_view(&textBuffer),
  };
  lsp_send_response_success(ctx, req, lsp_hover_to_json(ctx, &hover));
  dynstring_destroy(&textBuffer);
}

static void lsp_handle_req_references(LspContext* ctx, const JRpcRequest* req) {
  LspTextDocPos docPos;
  if (UNLIKELY(!lsp_doc_pos_from_json(ctx, req->params, &docPos))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  const JsonVal ctxObj         = lsp_maybe_field(ctx, req->params, string_lit("context"));
  const JsonVal includeDeclVal = lsp_maybe_field(ctx, ctxObj, string_lit("includeDeclaration"));
  const bool    includeDecl    = lsp_maybe_bool(ctx, includeDeclVal);

  if (ctx->flags & LspFlags_Trace) {
    const String txt = fmt_write_scratch("References: {}", fmt_text(lsp_doc_pos_scratch(&docPos)));
    lsp_send_trace(ctx, txt);
  }

  const ScriptDoc*    scriptDoc  = docPos.doc->scriptDoc;
  const ScriptSymBag* scriptSyms = docPos.doc->scriptSyms;
  const ScriptExpr    scriptRoot = docPos.doc->scriptRoot;

  if (sentinel_check(scriptRoot)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // Script did not parse correctly (likely due to structural errors).
  }

  const ScriptExpr refExpr = script_expr_find(scriptDoc, scriptRoot, docPos.pos, null, null);
  if (sentinel_check(refExpr)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // No expression found at the given position.
  }
  const ScriptSym sym = script_sym_find(scriptSyms, scriptDoc, refExpr);
  if (sentinel_check(sym)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // No symbol found for the expression.
  }

  const JsonVal locationsArr = json_add_array(ctx->jDoc);
  if (includeDecl) {
    const ScriptRange symRange = script_sym_location(scriptSyms, sym);
    if (script_range_valid(symRange)) {
      const LspLocation location = lsp_doc_location(docPos.doc, symRange);
      json_add_elem(ctx->jDoc, locationsArr, lsp_location_to_json(ctx, &location));
    }
  }
  const ScriptSymRefSet refs = script_sym_refs(scriptSyms, sym);
  for (const ScriptSymRef* ref = refs.begin; ref != refs.end; ++ref) {
    const LspLocation location = lsp_doc_location(docPos.doc, ref->location);
    json_add_elem(ctx->jDoc, locationsArr, lsp_location_to_json(ctx, &location));
  }
  lsp_send_response_success(ctx, req, locationsArr);
}

static bool lsp_sym_can_rename(const ScriptSymKind symKind) {
  switch (symKind) {
  case ScriptSymKind_Variable:
  case ScriptSymKind_MemoryKey:
    return true;
  default:
    return false;
  }
}

static bool lsp_sym_validate_id(const String str) {
  const ScriptLexFlags flags = ScriptLexFlags_NoWhitespace | ScriptLexFlags_IncludeComments;
  ScriptToken          token;
  const String         rem = script_lex(str, null, &token, flags);
  return string_is_empty(rem) && token.kind == ScriptTokenKind_Identifier;
}

static bool lsp_sym_validate_key(const String str) {
  const ScriptLexFlags flags = ScriptLexFlags_NoWhitespace | ScriptLexFlags_IncludeComments;
  ScriptToken          token;
  const String         rem = script_lex(str, null, &token, flags);
  return string_is_empty(rem) && token.kind == ScriptTokenKind_Key;
}

static bool lsp_sym_validate_name(const ScriptSymKind symKind, const String str) {
  switch (symKind) {
  case ScriptSymKind_Variable:
    return lsp_sym_validate_id(str);
  case ScriptSymKind_MemoryKey:
    return lsp_sym_validate_key(str);
  default:
    return false;
  }
}

static void lsp_handle_req_rename(LspContext* ctx, const JRpcRequest* req) {
  LspTextDocPos docPos;
  if (UNLIKELY(!lsp_doc_pos_from_json(ctx, req->params, &docPos))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  const String new = lsp_maybe_str(ctx, lsp_maybe_field(ctx, req->params, string_lit("newName")));

  if (ctx->flags & LspFlags_Trace) {
    const String txt = fmt_write_scratch(
        "Rename: {} -> '{}'", fmt_text(lsp_doc_pos_scratch(&docPos)), fmt_text(new));
    lsp_send_trace(ctx, txt);
  }

  const ScriptDoc*    scriptDoc    = docPos.doc->scriptDoc;
  const ScriptSymBag* scriptSyms   = docPos.doc->scriptSyms;
  const ScriptLookup* scriptLookup = docPos.doc->scriptLookup;
  const ScriptExpr    scriptRoot   = docPos.doc->scriptRoot;

  if (sentinel_check(scriptRoot)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // Script did not parse correctly (likely due to structural errors).
  }

  const ScriptExpr refExpr = script_expr_find(scriptDoc, scriptRoot, docPos.pos, null, null);
  if (sentinel_check(refExpr)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorRenameFailed);
    return; // No expression found at the given position.
  }
  const ScriptSym sym = script_sym_find(scriptSyms, scriptDoc, refExpr);
  if (sentinel_check(sym) || !lsp_sym_can_rename(script_sym_kind(scriptSyms, sym))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorRenameFailed);
    return; // Symbol not found or cannot be renamed.
  }
  if (!lsp_sym_validate_name(script_sym_kind(scriptSyms, sym), new)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidSymbolName);
    return; // Invalid new-name.
  }

  const JsonVal workspaceEditObj = json_add_object(ctx->jDoc);

  const JsonVal changesObj = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, workspaceEditObj, "changes", changesObj);

  const JsonVal editsArr = json_add_array(ctx->jDoc);
  json_add_field_str(ctx->jDoc, changesObj, docPos.doc->identifier, editsArr);

  // Rename the symbol itself.
  const ScriptRange symRange = script_sym_location(scriptSyms, sym);
  if (script_range_valid(symRange)) {
    const ScriptRangeLineCol rangeLc = script_lookup_range_to_line_col(scriptLookup, symRange);
    const LspTextEdit        edit    = {.range = rangeLc, .newText = new};
    json_add_elem(ctx->jDoc, editsArr, lsp_text_edit_to_json(ctx, &edit));
  }

  // Rename the references.
  const ScriptSymRefSet refs = script_sym_refs(scriptSyms, sym);
  for (const ScriptSymRef* ref = refs.begin; ref != refs.end; ++ref) {
    const ScriptRangeLineCol locLc = script_lookup_range_to_line_col(scriptLookup, ref->location);
    const LspTextEdit        edit  = {.range = locLc, .newText = new};
    json_add_elem(ctx->jDoc, editsArr, lsp_text_edit_to_json(ctx, &edit));
  }

  lsp_send_response_success(ctx, req, workspaceEditObj);
}

typedef struct {
  ScriptRange ranges[16];
  usize       count;
} LspSelectionRangesCtx;

/**
 * Collect a hierarchy of ranges corresponding to AST nodes containing the specified position.
 */
static bool lsp_find_selection_ranges(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  LspSelectionRangesCtx* rangesCtx = ctx;
  if (rangesCtx->count != array_elems(rangesCtx->ranges)) {
    rangesCtx->ranges[rangesCtx->count++] = script_expr_range(doc, expr);
  }
  return false; // Return false to visit all parent expressions as well.
}

static void lsp_handle_req_selection_range(LspContext* ctx, const JRpcRequest* req) {
  const LspDocument* doc = lsp_doc_from_json(ctx, req->params);
  if (UNLIKELY(!doc)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  const JsonVal positionsArr = lsp_maybe_field(ctx, req->params, string_lit("positions"));
  if (sentinel_check(positionsArr) || json_type(ctx->jDoc, positionsArr) != JsonType_Array) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  const ScriptDoc* scriptDoc  = doc->scriptDoc;
  const ScriptExpr scriptRoot = doc->scriptRoot;

  const JsonVal resArr = json_add_array(ctx->jDoc);
  json_for_elems(ctx->jDoc, positionsArr, posObj) {
    ScriptPosLineCol posLc;
    if (!lsp_position_from_json(ctx, posObj, &posLc) || sentinel_check(scriptRoot)) {
      json_add_elem(ctx->jDoc, resArr, lsp_selection_range_empty_to_json(ctx));
      continue; // Invalid position or script did not parse correctly.
    }
    const ScriptPos pos = script_lookup_from_line_col(doc->scriptLookup, posLc);
    if (sentinel_check(pos)) {
      json_add_elem(ctx->jDoc, resArr, lsp_selection_range_empty_to_json(ctx));
      continue; // Position out of bounds.
    }

    // Collect hierarchy of ranges at the given position.
    LspSelectionRangesCtx rangesCtx = {0};
    script_expr_find(scriptDoc, scriptRoot, pos, &rangesCtx, lsp_find_selection_ranges);

    // Convert ranges to line-column format and add them to the result.
    ScriptRangeLineCol rangesLc[array_elems(rangesCtx.ranges)];
    for (usize i = 0; i != rangesCtx.count; ++i) {
      rangesLc[i] = script_lookup_range_to_line_col(doc->scriptLookup, rangesCtx.ranges[i]);
    }
    json_add_elem(ctx->jDoc, resArr, lsp_selection_range_to_json(ctx, rangesLc, rangesCtx.count));
  }
  lsp_send_response_success(ctx, req, resArr);
}

static bool lsp_semantic_token_sym_enabled(const ScriptSymKind kind) {
  switch (kind) {
  case ScriptSymKind_BuiltinConstant:
  case ScriptSymKind_BuiltinFunction:
  case ScriptSymKind_ExternFunction:
  case ScriptSymKind_Variable:
    return true;
  default:
    return false;
  }
}

static LspSemanticTokenType lsp_semantic_token_sym_type(const ScriptSymKind kind) {
  switch (kind) {
  case ScriptSymKind_BuiltinConstant:
    return LspSemanticTokenType_Enum;
  case ScriptSymKind_BuiltinFunction:
  case ScriptSymKind_ExternFunction:
    return LspSemanticTokenType_Function;
  case ScriptSymKind_Variable:
    return LspSemanticTokenType_Variable;
  default:
    break;
  }
  diag_crash_msg("Unsupported symbol kind");
}

static LspSemanticTokenMod lsp_semantic_token_sym_mod(const ScriptSymKind kind) {
  switch (kind) {
  case ScriptSymKind_BuiltinConstant:
    return LspSemanticTokenMod_ReadOnly;
  default:
    return LspSemanticTokenMod_None;
  }
}

static LspSemanticTokenMod lsp_semantic_token_ref_mod(const ScriptSymRef* ref) {
  LspSemanticTokenMod mod = LspSemanticTokenMod_None;
  if (ref->kind == ScriptSymRefKind_Write) {
    mod |= LspSemanticTokenMod_Modification;
  }
  return mod;
}

static void lsp_handle_req_semantic_tokens(LspContext* ctx, const JRpcRequest* req) {
  const LspDocument* doc = lsp_doc_from_json(ctx, req->params);
  if (UNLIKELY(!doc)) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  const ScriptSymBag* scriptSyms   = doc->scriptSyms;
  const ScriptLookup* scriptLookup = doc->scriptLookup;

  LspSemanticToken tokens[4096];
  usize            tokenCount = 0;

  // Gather tokens from symbols.
  ScriptSym sym = script_sym_first(scriptSyms, script_pos_sentinel);
  for (; !sentinel_check(sym); sym = script_sym_next(scriptSyms, script_pos_sentinel, sym)) {
    const ScriptSymKind symKind = script_sym_kind(scriptSyms, sym);
    if (!lsp_semantic_token_sym_enabled(symKind)) {
      continue;
    }
    const LspSemanticTokenType tokType = lsp_semantic_token_sym_type(symKind);
    const LspSemanticTokenMod  tokMod  = lsp_semantic_token_sym_mod(symKind);

    // Add symbol definition token.
    const ScriptRange symLoc = script_sym_location(scriptSyms, sym);
    if (script_range_valid(symLoc)) {
      const ScriptRangeLineCol symLocLc = script_lookup_range_to_line_col(scriptLookup, symLoc);
      if (UNLIKELY(symLocLc.start.line != symLocLc.end.line)) {
        continue; // Multi-line tokens are not supported.
      }
      if (UNLIKELY(tokenCount == array_elems(tokens))) {
        break; // Token limit reached.
      }
      tokens[tokenCount++] = (LspSemanticToken){
          .pos    = symLocLc.start,
          .length = symLoc.end - symLoc.start,
          .type   = tokType,
          .mod    = tokMod | LspSemanticTokenMod_Definition,
      };
    }

    // Add symbol reference tokens.
    const ScriptSymRefSet refs = script_sym_refs(scriptSyms, sym);
    for (const ScriptSymRef* ref = refs.begin; ref != refs.end; ++ref) {
      ScriptRangeLineCol refLoc = script_lookup_range_to_line_col(scriptLookup, ref->location);
      if (UNLIKELY(refLoc.start.line != refLoc.end.line)) {
        continue; // Multi-line tokens are not supported.
      }
      if (UNLIKELY(tokenCount == array_elems(tokens))) {
        break; // Token limit reached.
      }
      tokens[tokenCount++] = (LspSemanticToken){
          .pos    = refLoc.start,
          .length = refLoc.end.column - refLoc.start.column,
          .type   = tokType,
          .mod    = tokMod | lsp_semantic_token_ref_mod(ref),
      };
    }
  }

  // Sort tokens by position.
  sort_quicksort_t(tokens, tokens + tokenCount, LspSemanticToken, lsp_semantic_token_compare);

  // Send the response.
  const JsonVal res = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, res, "data", lsp_semantic_tokens_to_json(ctx, tokens, tokenCount));
  lsp_send_response_success(ctx, req, res);
}

typedef struct {
  const ScriptSymBag* symBag;
  ScriptPos           cursor;
} LspSignatureHelpContext;

/**
 * Predicate for finding call expressions where the cursor is inside the argument list.
 */
static bool lsp_pred_signature_help(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  LspSignatureHelpContext* sigHelpCtx = ctx;

  const ScriptSym sym = script_sym_find(sigHelpCtx->symBag, doc, expr);
  if (sentinel_check(sym)) {
    return false; // No symbol known.
  }
  if (!script_sym_sig(sigHelpCtx->symBag, sym)) {
    return false; // No signature known (not a call expression).
  }
  const String      label = script_sym_label(sigHelpCtx->symBag, sym);
  const ScriptRange range = script_expr_range(doc, expr);
  diag_assert(script_range_contains(range, sigHelpCtx->cursor));

  // Exclude calls where the cursor is on the identifier label instead of the argument list.
  const u32 relCursor = sigHelpCtx->cursor - range.start;
  return relCursor >= (label.size + 1);
}

static void lsp_handle_req_signature_help(LspContext* ctx, const JRpcRequest* req) {
  LspTextDocPos docPos;
  if (UNLIKELY(!lsp_doc_pos_from_json(ctx, req->params, &docPos))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
    return;
  }

  if (ctx->flags & LspFlags_Trace) {
    const String txt = fmt_write_scratch("Signature: {}", fmt_text(lsp_doc_pos_scratch(&docPos)));
    lsp_send_trace(ctx, txt);
  }

  const ScriptDoc*    scriptDoc  = docPos.doc->scriptDoc;
  const ScriptSymBag* scriptSyms = docPos.doc->scriptSyms;
  const ScriptExpr    scriptRoot = docPos.doc->scriptRoot;

  if (sentinel_check(scriptRoot)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // Script did not parse correctly (likely due to structural errors).
  }

  LspSignatureHelpContext sigHelpCtx = {
      .symBag = scriptSyms,
      .cursor = docPos.pos,
  };

  const ScriptExpr callExpr =
      script_expr_find(scriptDoc, scriptRoot, docPos.pos, &sigHelpCtx, lsp_pred_signature_help);

  if (sentinel_check(callExpr)) {
    lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
    return; // No call expression at the given position.
  }

  const ScriptSym    callSym = script_sym_find(scriptSyms, scriptDoc, callExpr);
  const LspSignature sig     = {
      .label     = script_sym_label(scriptSyms, callSym),
      .doc       = script_sym_doc(scriptSyms, callSym),
      .scriptSig = script_sym_sig(scriptSyms, callSym),
  };

  const JsonVal signaturesArr = json_add_array(ctx->jDoc);
  json_add_elem(ctx->jDoc, signaturesArr, lsp_signature_to_json(ctx, &sig));

  const JsonVal sigHelp = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, sigHelp, "signatures", signaturesArr);

  u32 index = 0;
  if (script_expr_arg_count(scriptDoc, callExpr)) {
    // When providing arguments check which argument position is being hovered.
    index = script_expr_arg_index(scriptDoc, callExpr, docPos.pos);
  }
  if (script_sig_arg_max_count(sig.scriptSig) == u8_max) {
    // For variable argument count signatures always return the last argument when out of bounds.
    index = math_min(index, (u32)(script_sig_arg_count(sig.scriptSig) - 1));
  }
  json_add_field_lit(ctx->jDoc, sigHelp, "activeParameter", json_add_number(ctx->jDoc, index));

  lsp_send_response_success(ctx, req, sigHelp);
}

static void lsp_handle_req(LspContext* ctx, const JRpcRequest* req) {
  static const struct {
    String method;
    void (*handler)(LspContext*, const JRpcRequest*);
  } g_handlers[] = {
      {string_static("initialize"), lsp_handle_req_initialize},
      {string_static("shutdown"), lsp_handle_req_shutdown},
      {string_static("textDocument/colorPresentation"), lsp_handle_req_color_representation},
      {string_static("textDocument/completion"), lsp_handle_req_completion},
      {string_static("textDocument/definition"), lsp_handle_req_definition},
      {string_static("textDocument/documentColor"), lsp_handle_req_color},
      {string_static("textDocument/documentHighlight"), lsp_handle_req_highlight},
      {string_static("textDocument/documentSymbol"), lsp_handle_req_symbols},
      {string_static("textDocument/formatting"), lsp_handle_req_formatting},
      {string_static("textDocument/hover"), lsp_handle_req_hover},
      {string_static("textDocument/references"), lsp_handle_req_references},
      {string_static("textDocument/rename"), lsp_handle_req_rename},
      {string_static("textDocument/selectionRange"), lsp_handle_req_selection_range},
      {string_static("textDocument/semanticTokens/full"), lsp_handle_req_semantic_tokens},
      {string_static("textDocument/signatureHelp"), lsp_handle_req_signature_help},
  };

  for (u32 i = 0; i != array_elems(g_handlers); ++i) {
    if (string_eq(req->method, g_handlers[i].method)) {
      g_handlers[i].handler(ctx, req);
      return;
    }
  }
  lsp_send_response_error(ctx, req, &g_jrpcErrorMethodNotFound);
}

static void lsp_handle_jrpc(LspContext* ctx, const LspHeader* header, const JsonVal value) {
  const String version = lsp_maybe_str(ctx, lsp_maybe_field(ctx, value, string_lit("jsonrpc")));
  if (UNLIKELY(!string_eq(version, string_lit("2.0")))) {
    ctx->status = LspStatus_ErrorUnsupportedJRpcVersion;
    return;
  }
  const String method = lsp_maybe_str(ctx, lsp_maybe_field(ctx, value, string_lit("method")));
  if (UNLIKELY(string_is_empty(method))) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  const JsonVal params = lsp_maybe_field(ctx, value, string_lit("params"));
  const JsonVal id     = lsp_maybe_field(ctx, value, string_lit("id"));

  const TimeSteady startTime       = time_steady_clock();
  const usize      startBytesOut   = ctx->bytesOut;
  const u64        startHeapAllocs = alloc_stats_query().heapCounter;

  if (sentinel_check(id)) {
    lsp_handle_notif(ctx, &(JRpcNotification){.method = method, .params = params});
  } else {
    lsp_handle_req(ctx, &(JRpcRequest){.method = method, .params = params, .id = id});
  }

  if (ctx->flags & LspFlags_Profile) {
    const TimeDuration dur        = time_steady_duration(startTime, time_steady_clock());
    const usize        bytesOut   = ctx->bytesOut - startBytesOut;
    const u64          heapAllocs = alloc_stats_query().heapCounter - startHeapAllocs;

    const String text = fmt_write_scratch(
        "[Profile] dur: {<7} in: {<8} out: {<8} allocs: {<4} ({})",
        fmt_duration(dur),
        fmt_size(header->contentLength),
        fmt_size(bytesOut),
        fmt_int(heapAllocs),
        fmt_text(method));
    lsp_send_info(ctx, text);
  }
}

static i32 lsp_run_stdio(ScriptBinder* scriptBinders[], const u32 scriptBinderCount) {
  DynString readBuffer  = dynstring_create(g_allocHeap, 8 * usize_kibibyte);
  DynString writeBuffer = dynstring_create(g_allocHeap, 2 * usize_kibibyte);
  JsonDoc*  jDoc        = json_create(g_allocHeap, 1024);
  DynArray  openDocs    = dynarray_create_t(g_allocHeap, LspDocument, 16);

  LspContext ctx = {
      .status       = LspStatus_Running,
      .readBuffer   = &readBuffer,
      .writeBuffer  = &writeBuffer,
      .scriptBinder = scriptBinderCount ? scriptBinders[0] : null,
      .jDoc         = jDoc,
      .openDocs     = &openDocs,
      .in           = g_fileStdIn,
      .out          = g_fileStdOut,
  };

  lsp_send_info(&ctx, string_lit("Server starting up"));
  for (u32 i = 0; i != scriptBinderCount; ++i) {
    lsp_send_info(
        &ctx,
        fmt_write_scratch(
            "Loaded script-binder '{}' ({} functions)",
            fmt_text(script_binder_name(scriptBinders[i])),
            fmt_int(script_binder_count(scriptBinders[i]))));
  }

  while (LIKELY(ctx.status == LspStatus_Running)) {
    const LspHeader header  = lsp_read_header(&ctx);
    const String    content = lsp_read_sized(&ctx, header.contentLength);

    JsonResult jsonResult;
    json_read(jDoc, content, JsonReadFlags_None, &jsonResult);
    if (UNLIKELY(jsonResult.type == JsonResultType_Fail)) {
      const String jsonErr = json_error_str(jsonResult.error);
      file_write_sync(g_fileStdErr, fmt_write_scratch("lsp: Json-Error: {}\n", fmt_text(jsonErr)));
      ctx.status = LspStatus_ErrorInvalidJson;
      break;
    }

    lsp_handle_jrpc(&ctx, &header, jsonResult.val);

    lsp_read_trim(&ctx);
    json_clear(jDoc);
  }

  json_destroy(jDoc);
  dynstring_destroy(&readBuffer);
  dynstring_destroy(&writeBuffer);

  dynarray_for_t(&openDocs, LspDocument, doc) { lsp_doc_destroy(doc); };
  dynarray_destroy(&openDocs);

  if (ctx.status != LspStatus_Exit) {
    const String errorMsg = g_lspStatusMessage[ctx.status];
    file_write_sync(g_fileStdErr, fmt_write_scratch("lsp: {}\n", fmt_text(errorMsg)));
    return 1;
  }
  return 0;
}

static ScriptBinder* lsp_read_binder_file(const String path) {
  ScriptBinder* out = null;
  File*         file;
  FileResult    fileRes;
  if ((fileRes = file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file))) {
    file_write_sync(g_fileStdErr, string_lit("lsp: Failed to open binder file.\n"));
    goto Ret;
  }
  String fileData;
  if ((fileRes = file_map(file, &fileData, FileHints_Prefetch))) {
    file_write_sync(g_fileStdErr, string_lit("lsp: Failed to map binder file.\n"));
    goto Ret;
  }
  out = script_binder_read(g_allocHeap, fileData);
  if (!out) {
    file_write_sync(g_fileStdErr, string_lit("lsp: Invalid binder file.\n"));
    goto Ret;
  }
Ret:
  if (file) {
    file_destroy(file);
  }
  return out;
}

static CliId g_optStdio, g_optBinders, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo Script Language Server"));

  g_optStdio = cli_register_flag(app, 0, string_lit("stdio"), CliOptionFlags_None);
  cli_register_desc(app, g_optStdio, string_lit("Use stdin and stdout for communication."));

  g_optBinders = cli_register_flag(app, 'b', string_lit("binders"), CliOptionFlags_MultiValue);
  const String binderDesc = string_lit("Script binder schemas to use."
                                       "\nFirst matching binder is used per doc.");
  cli_register_desc(app, g_optBinders, binderDesc);
  cli_register_validator(app, g_optBinders, cli_validate_file_regular);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optStdio);
  cli_register_exclusions(app, g_optHelp, g_optBinders);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  i32           exitCode = 0;
  ScriptBinder* scriptBinders[lsp_script_binders_max];
  u32           scriptBinderCount = 0;

  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    goto Exit;
  }

  const CliParseValues binderArg = cli_parse_values(invoc, g_optBinders);
  if (binderArg.count > lsp_script_binders_max) {
    file_write_sync(g_fileStdErr, string_lit("lsp: Binder count exceeds maximum.\n"));
    exitCode = 1;
    goto Exit;
  }
  for (; scriptBinderCount != binderArg.count; ++scriptBinderCount) {
    scriptBinders[scriptBinderCount] = lsp_read_binder_file(binderArg.values[scriptBinderCount]);
    if (!scriptBinders[scriptBinderCount]) {
      exitCode = 1;
      goto Exit;
    }
  }

  if (cli_parse_provided(invoc, g_optStdio)) {
    exitCode = lsp_run_stdio(scriptBinders, scriptBinderCount);
  } else {
    exitCode = 1;
    file_write_sync(g_fileStdErr, string_lit("lsp: No communication method specified.\n"));
  }

Exit:
  for (u32 i = 0; i != scriptBinderCount; ++i) {
    script_binder_destroy(scriptBinders[i]);
  }
  return exitCode;
}
