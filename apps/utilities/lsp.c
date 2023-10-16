#include "app_cli.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_format.h"
#include "core_path.h"
#include "json.h"
#include "script_binder.h"
#include "script_diag.h"
#include "script_read.h"
#include "script_sym.h"

/**
 * Language Server Protocol implementation for the Volo script language.
 *
 * Specification:
 * https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
 */

typedef enum {
  LspStatus_Running,
  LspStatus_Exit,
  LspStatus_ErrorReadFailed,
  LspStatus_ErrorInvalidJson,
  LspStatus_ErrorInvalidJRpcMessage,
  LspStatus_ErrorUnsupportedJRpcVersion,
  LspStatus_ErrorMalformedNotification,
  LspStatus_ErrorMalformedRequest,

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
    [LspStatus_ErrorMalformedRequest]       = string_static("Error: Malformed request"),
};

typedef enum {
  LspFlags_Initialized = 1 << 0,
  LspFlags_Shutdown    = 1 << 1,
  LspFlags_Trace       = 1 << 2,
} LspFlags;

typedef struct {
  String         identifier;
  String         text;
  ScriptDoc*     scriptDoc;
  ScriptDiagBag* scriptDiags;
  ScriptSymBag*  scriptSyms;
} LspDocument;

typedef struct {
  LspStatus     status;
  LspFlags      flags;
  DynString*    readBuffer;
  usize         readCursor;
  DynString*    writeBuffer;
  ScriptBinder* scriptBinder;
  JsonDoc*      jDoc;     // Cleared between messages.
  DynArray*     openDocs; // LspDocument[]*
  File*         in;
  File*         out;
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

typedef enum {
  LspDiagSeverity_Error       = 1,
  LspDiagSeverity_Warning     = 2,
  LspDiagSeverity_Information = 3,
  LspDiagSeverity_Hint        = 4,
} LspDiagSeverity;

typedef struct {
  ScriptRangeLineCol range;
  LspDiagSeverity    severity;
  String             message;
} LspDiag;

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
  String                labelDescription;
  LspCompletionItemKind kind : 8;
  u8                    commitChar;
} LspCompletionItem;

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

static void lsp_doc_destroy(LspDocument* doc) {
  string_free(g_alloc_heap, doc->identifier);
  string_maybe_free(g_alloc_heap, doc->text);
  script_destroy(doc->scriptDoc);
  script_diag_bag_destroy(doc->scriptDiags);
  script_sym_bag_destroy(doc->scriptSyms);
}

static void lsp_doc_update_text(LspDocument* doc, const String text) {
  string_maybe_free(g_alloc_heap, doc->text);
  doc->text = string_maybe_dup(g_alloc_heap, text);
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
      .identifier  = string_dup(g_alloc_heap, identifier),
      .text        = string_maybe_dup(g_alloc_heap, text),
      .scriptDoc   = script_create(g_alloc_heap),
      .scriptDiags = script_diag_bag_create(g_alloc_heap, ScriptDiagFilter_All),
      .scriptSyms  = script_sym_bag_create(g_alloc_heap),
  };

  return res;
}

static void lsp_doc_close(LspContext* ctx, LspDocument* doc) {
  lsp_doc_destroy(doc);

  const usize index = doc - dynarray_begin_t(ctx->openDocs, LspDocument);
  dynarray_remove_unordered(ctx->openDocs, index, 1);
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
    // Consume the rest of the line.
    const usize lineEndPos = string_find_first_char(input, '\n');
    input                  = string_consume(input, sentinel_check(lineEndPos) ? 0 : lineEndPos + 1);
  }
  return result;
}

static String lsp_maybe_str(LspContext* ctx, const JsonVal val) {
  if (sentinel_check(val) || json_type(ctx->jDoc, val) != JsonType_String) {
    return string_empty;
  }
  return json_string(ctx->jDoc, val);
}

static JsonVal lsp_maybe_field(LspContext* ctx, const JsonVal val, const String fieldName) {
  if (sentinel_check(val) || json_type(ctx->jDoc, val) != JsonType_Object) {
    return sentinel_u32;
  }
  return json_field(ctx->jDoc, val, fieldName);
}

static JsonVal lsp_maybe_elem(LspContext* ctx, const JsonVal val, const u32 index) {
  if (sentinel_check(val) || json_type(ctx->jDoc, val) != JsonType_Array) {
    return sentinel_u32;
  }
  return json_elem(ctx->jDoc, val, index);
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

static JsonVal lsp_completion_item_to_json(LspContext* ctx, const LspCompletionItem* item) {
  JsonVal labelDetailsObj = sentinel_u32;
  if (!string_is_empty(item->labelDetail) || !string_is_empty(item->labelDescription)) {
    labelDetailsObj = json_add_object(ctx->jDoc);

    if (!string_is_empty(item->labelDetail)) {
      const JsonVal detailVal = json_add_string(ctx->jDoc, item->labelDetail);
      json_add_field_lit(ctx->jDoc, labelDetailsObj, "detail", detailVal);
    }
    if (!string_is_empty(item->labelDescription)) {
      const JsonVal descVal = json_add_string(ctx->jDoc, item->labelDescription);
      json_add_field_lit(ctx->jDoc, labelDetailsObj, "description", descVal);
    }
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
  json_add_field_lit(ctx->jDoc, obj, "kind", json_add_number(ctx->jDoc, item->kind));
  json_add_field_lit(ctx->jDoc, obj, "commitCharacters", commitCharsArr);
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

static void lsp_update_trace_config(LspContext* ctx, const JsonVal traceValue) {
  if (string_eq(lsp_maybe_str(ctx, traceValue), string_lit("off"))) {
    ctx->flags &= ~LspFlags_Trace;
  } else {
    ctx->flags |= LspFlags_Trace;
  }
}

static void lsp_send_json(LspContext* ctx, const JsonVal val) {
  const JsonWriteOpts writeOpts = json_write_opts(.flags = JsonWriteFlags_None);
  json_write(ctx->writeBuffer, ctx->jDoc, val, &writeOpts);

  const usize  contentSize = ctx->writeBuffer->size;
  const String headerText  = fmt_write_scratch("Content-Length: {}\r\n\r\n", fmt_int(contentSize));
  dynstring_insert(ctx->writeBuffer, headerText, 0);

  file_write_sync(ctx->out, dynstring_view(ctx->writeBuffer));
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
    const JsonVal diag = json_add_object(ctx->jDoc);
    json_add_field_lit(ctx->jDoc, diag, "range", lsp_range_to_json(ctx, &values[i].range));
    json_add_field_lit(ctx->jDoc, diag, "severity", json_add_number(ctx->jDoc, values[i].severity));
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

static void lsp_handle_notif_initialized(LspContext* ctx, const JRpcNotification* notif) {
  (void)notif;
  ctx->flags |= LspFlags_Initialized;

  lsp_send_info(ctx, string_lit("Server successfully initialized"));
}

static void lsp_handle_notif_exit(LspContext* ctx, const JRpcNotification* notif) {
  (void)notif;
  ctx->status = LspStatus_Exit;
}

static void lsp_handle_notif_set_trace(LspContext* ctx, const JRpcNotification* notif) {
  const JsonVal traceVal = lsp_maybe_field(ctx, notif->params, string_lit("value"));
  if (UNLIKELY(sentinel_check(traceVal))) {
    goto Error;
  }
  lsp_update_trace_config(ctx, traceVal);
  return;

Error:
  ctx->status = LspStatus_ErrorMalformedNotification;
}

static void lsp_analyze_doc(LspContext* ctx, LspDocument* doc) {
  script_clear(doc->scriptDoc);
  script_diag_clear(doc->scriptDiags);
  script_sym_clear(doc->scriptSyms);

  const TimeSteady readStartTime = time_steady_clock();

  script_read(doc->scriptDoc, ctx->scriptBinder, doc->text, doc->scriptDiags, doc->scriptSyms);

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

    LspDiagSeverity severity;
    switch (diag->type) {
    case ScriptDiagType_Error:
      severity = LspDiagSeverity_Error;
      break;
    case ScriptDiagType_Warning:
      severity = LspDiagSeverity_Warning;
      break;
    }

    /**
     * TODO: The columns offsets we compute are in unicode codepoints (utf32). However we report to
     * the LSP client that we are using utf16 offsets. Reason is that some clients (for example
     * VCCode) only support utf16 offsets. This means the column offsets are incorrect if the line
     * contains unicode characters outside of the utf16 range.
     */
    lspDiags[i] = (LspDiag){
        .range    = script_range_to_line_col(doc->text, diag->range),
        .severity = severity,
        .message  = script_diag_msg_scratch(doc->text, diag),
    };
  }
  lsp_send_diagnostics(ctx, doc->identifier, lspDiags, lspDiagCount);
}

static void lsp_handle_notif_doc_did_open(LspContext* ctx, const JRpcNotification* notif) {
  const JsonVal docVal = lsp_maybe_field(ctx, notif->params, string_lit("textDocument"));
  const String  uri    = lsp_maybe_str(ctx, lsp_maybe_field(ctx, docVal, string_lit("uri")));
  if (UNLIKELY(string_is_empty(uri))) {
    goto Error;
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
  return;

Error:
  ctx->status = LspStatus_ErrorMalformedNotification;
}

static void lsp_handle_notif_doc_did_change(LspContext* ctx, const JRpcNotification* notif) {
  const JsonVal docVal = lsp_maybe_field(ctx, notif->params, string_lit("textDocument"));
  const String  uri    = lsp_maybe_str(ctx, lsp_maybe_field(ctx, docVal, string_lit("uri")));
  if (UNLIKELY(string_is_empty(uri))) {
    goto Error;
  }
  const JsonVal changesVal    = lsp_maybe_field(ctx, notif->params, string_lit("contentChanges"));
  const JsonVal changeZeroVal = lsp_maybe_elem(ctx, changesVal, 0);
  const String  text = lsp_maybe_str(ctx, lsp_maybe_field(ctx, changeZeroVal, string_lit("text")));

  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Document update: {}", fmt_text(uri)));
  }

  LspDocument* doc = lsp_doc_find(ctx, uri);
  if (LIKELY(doc)) {
    lsp_doc_update_text(doc, text);
    lsp_analyze_doc(ctx, doc);
  } else {
    lsp_send_error(ctx, fmt_write_scratch("Document not open: {}", fmt_text(uri)));
  }
  return;

Error:
  ctx->status = LspStatus_ErrorMalformedNotification;
}

static void lsp_handle_notif_doc_did_close(LspContext* ctx, const JRpcNotification* notif) {
  const JsonVal docVal = lsp_maybe_field(ctx, notif->params, string_lit("textDocument"));
  const String  uri    = lsp_maybe_str(ctx, lsp_maybe_field(ctx, docVal, string_lit("uri")));
  if (UNLIKELY(string_is_empty(uri))) {
    goto Error;
  }
  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Document close: {}", fmt_text(uri)));
  }

  LspDocument* doc = lsp_doc_find(ctx, uri);
  if (LIKELY(doc)) {
    lsp_doc_close(ctx, doc);
    lsp_send_diagnostics(ctx, uri, null, 0);
  } else {
    lsp_send_error(ctx, fmt_write_scratch("Document not open: {}", fmt_text(uri)));
  }
  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Document count: {}", fmt_int(ctx->openDocs->size)));
  }
  return;

Error:
  ctx->status = LspStatus_ErrorMalformedNotification;
}

static void lsp_handle_notif(LspContext* ctx, const JRpcNotification* notif) {
  static const struct {
    String method;
    void (*handler)(LspContext*, const JRpcNotification*);
  } g_handlers[] = {
      {string_static("initialized"), lsp_handle_notif_initialized},
      {string_static("exit"), lsp_handle_notif_exit},
      {string_static("$/setTrace"), lsp_handle_notif_set_trace},
      {string_static("textDocument/didOpen"), lsp_handle_notif_doc_did_open},
      {string_static("textDocument/didChange"), lsp_handle_notif_doc_did_change},
      {string_static("textDocument/didClose"), lsp_handle_notif_doc_did_close},
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

static void lsp_handle_req_initialize(LspContext* ctx, const JRpcRequest* req) {
  const JsonVal traceVal = lsp_maybe_field(ctx, req->params, string_lit("trace"));
  if (!sentinel_check(traceVal)) {
    lsp_update_trace_config(ctx, traceVal);
  }

  const JsonVal docSyncOpts = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, docSyncOpts, "openClose", json_add_bool(ctx->jDoc, true));
  json_add_field_lit(ctx->jDoc, docSyncOpts, "change", json_add_number(ctx->jDoc, 1));

  const JsonVal completionTriggerCharArr = json_add_array(ctx->jDoc);
  json_add_elem(ctx->jDoc, completionTriggerCharArr, json_add_string_lit(ctx->jDoc, "$"));

  const JsonVal completionOpts = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, completionOpts, "resolveProvider", json_add_bool(ctx->jDoc, false));
  json_add_field_lit(ctx->jDoc, completionOpts, "triggerCharacters", completionTriggerCharArr);

  const JsonVal formattingOpts = json_add_object(ctx->jDoc);

  const JsonVal capabilities = json_add_object(ctx->jDoc);
  // NOTE: At the time of writing VSCode only supports utf-16 position encoding.
  const JsonVal positionEncoding = json_add_string_lit(ctx->jDoc, "utf-16");
  json_add_field_lit(ctx->jDoc, capabilities, "positionEncoding", positionEncoding);
  json_add_field_lit(ctx->jDoc, capabilities, "textDocumentSync", docSyncOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "completionProvider", completionOpts);
  json_add_field_lit(ctx->jDoc, capabilities, "documentFormattingProvider", formattingOpts);

  const JsonVal info          = json_add_object(ctx->jDoc);
  const JsonVal serverName    = json_add_string_lit(ctx->jDoc, "Volo Language Server");
  const JsonVal serverVersion = json_add_string_lit(ctx->jDoc, "0.1");
  json_add_field_lit(ctx->jDoc, info, "name", serverName);
  json_add_field_lit(ctx->jDoc, info, "version", serverVersion);

  const JsonVal result = json_add_object(ctx->jDoc);
  json_add_field_lit(ctx->jDoc, result, "capabilities", capabilities);
  json_add_field_lit(ctx->jDoc, result, "serverInfo", info);

  lsp_send_response_success(ctx, req, result);
  return;
}

static void lsp_handle_req_shutdown(LspContext* ctx, const JRpcRequest* req) {
  ctx->flags |= LspFlags_Shutdown;
  lsp_send_response_success(ctx, req, json_add_null(ctx->jDoc));
}

static LspCompletionItemKind lsp_completion_kind_for_sym(const ScriptSym* sym) {
  switch (sym->type) {
  case ScriptSymType_Keyword:
    return LspCompletionItemKind_Keyword;
  case ScriptSymType_BuiltinConstant:
    return LspCompletionItemKind_Constant;
  case ScriptSymType_BuiltinFunction:
    // NOTE: This is taking some creative liberties with the 'Constructor' meaning.
    return LspCompletionItemKind_Constructor;
  case ScriptSymType_ExternFunction:
    return LspCompletionItemKind_Function;
  case ScriptSymType_Variable:
    return LspCompletionItemKind_Variable;
  case ScriptSymType_MemoryKey:
    return LspCompletionItemKind_Property;
  case ScriptSymType_Count:
    break;
  }
  diag_crash();
}

static void lsp_handle_req_completion(LspContext* ctx, const JRpcRequest* req) {
  const JsonVal docVal = lsp_maybe_field(ctx, req->params, string_lit("textDocument"));
  const String  uri    = lsp_maybe_str(ctx, lsp_maybe_field(ctx, docVal, string_lit("uri")));
  if (UNLIKELY(string_is_empty(uri))) {
    goto InvalidParams;
  }
  const JsonVal    posLineColVal = lsp_maybe_field(ctx, req->params, string_lit("position"));
  ScriptPosLineCol posLineCol;
  if (UNLIKELY(!lsp_position_from_json(ctx, posLineColVal, &posLineCol))) {
    goto InvalidParams;
  }

  LspDocument* doc = lsp_doc_find(ctx, uri);
  if (UNLIKELY(!doc)) {
    goto InvalidParams; // TODO: Make a unique error respose for the 'document not open' case.
  }

  const ScriptPos pos = script_pos_from_line_col(doc->text, posLineCol);
  if (UNLIKELY(sentinel_check(pos))) {
    goto InvalidParams; // TODO: Make a unique error respose for the 'position out of range' case.
  }

  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(
        ctx,
        fmt_write_scratch(
            "Complete: {} [{}:{}]",
            fmt_text(uri),
            fmt_int(posLineCol.line + 1),
            fmt_int(posLineCol.column + 1)));
  }

  const JsonVal itemsArr = json_add_array(ctx->jDoc);

  ScriptSymId itr = script_sym_first(doc->scriptSyms, pos);
  for (; !sentinel_check(itr); itr = script_sym_next(doc->scriptSyms, pos, itr)) {
    const ScriptSym*        sym            = script_sym_data(doc->scriptSyms, itr);
    const LspCompletionItem completionItem = {
        .label            = sym->label,
        .labelDetail      = script_sym_is_func(sym) ? string_lit("()") : string_empty,
        .labelDescription = script_sym_type_str(sym->type),
        .kind             = lsp_completion_kind_for_sym(sym),
        .commitChar       = script_sym_is_func(sym) ? '(' : ' ',
    };
    json_add_elem(ctx->jDoc, itemsArr, lsp_completion_item_to_json(ctx, &completionItem));
  }
  lsp_send_response_success(ctx, req, itemsArr);
  return;

InvalidParams:
  lsp_send_response_error(ctx, req, &g_jrpcErrorInvalidParams);
}

static void lsp_handle_req(LspContext* ctx, const JRpcRequest* req) {
  static const struct {
    String method;
    void (*handler)(LspContext*, const JRpcRequest*);
  } g_handlers[] = {
      {string_static("initialize"), lsp_handle_req_initialize},
      {string_static("shutdown"), lsp_handle_req_shutdown},
      {string_static("textDocument/completion"), lsp_handle_req_completion},
  };

  for (u32 i = 0; i != array_elems(g_handlers); ++i) {
    if (string_eq(req->method, g_handlers[i].method)) {
      g_handlers[i].handler(ctx, req);
      return;
    }
  }
  lsp_send_response_error(ctx, req, &g_jrpcErrorMethodNotFound);
}

static void lsp_handle_jrpc(LspContext* ctx, const JsonVal value) {
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

  if (sentinel_check(id)) {
    lsp_handle_notif(ctx, &(JRpcNotification){.method = method, .params = params});
  } else {
    lsp_handle_req(ctx, &(JRpcRequest){.method = method, .params = params, .id = id});
  }
}

static ScriptBinder* lsp_script_binder_create() {
  ScriptBinder* binder = script_binder_create(g_alloc_heap);

  // TODO: Instead of manually listing the supported bindings here we should read them from a file.
  script_binder_declare(binder, string_lit("self"), null);
  script_binder_declare(binder, string_lit("exists"), null);
  script_binder_declare(binder, string_lit("position"), null);
  script_binder_declare(binder, string_lit("rotation"), null);
  script_binder_declare(binder, string_lit("scale"), null);
  script_binder_declare(binder, string_lit("name"), null);
  script_binder_declare(binder, string_lit("faction"), null);
  script_binder_declare(binder, string_lit("health"), null);
  script_binder_declare(binder, string_lit("time"), null);
  script_binder_declare(binder, string_lit("nav_query"), null);
  script_binder_declare(binder, string_lit("nav_target"), null);
  script_binder_declare(binder, string_lit("line_of_sight"), null);
  script_binder_declare(binder, string_lit("capable"), null);
  script_binder_declare(binder, string_lit("active"), null);
  script_binder_declare(binder, string_lit("target_primary"), null);
  script_binder_declare(binder, string_lit("target_range_min"), null);
  script_binder_declare(binder, string_lit("target_range_max"), null);
  script_binder_declare(binder, string_lit("spawn"), null);
  script_binder_declare(binder, string_lit("destroy"), null);
  script_binder_declare(binder, string_lit("destroy_after"), null);
  script_binder_declare(binder, string_lit("teleport"), null);
  script_binder_declare(binder, string_lit("nav_travel"), null);
  script_binder_declare(binder, string_lit("nav_stop"), null);
  script_binder_declare(binder, string_lit("attach"), null);
  script_binder_declare(binder, string_lit("detach"), null);
  script_binder_declare(binder, string_lit("damage"), null);
  script_binder_declare(binder, string_lit("attack"), null);
  script_binder_declare(binder, string_lit("debug_log"), null);

  script_binder_finalize(binder);
  return binder;
}

static i32 lsp_run_stdio() {
  DynString     readBuffer   = dynstring_create(g_alloc_heap, 8 * usize_kibibyte);
  DynString     writeBuffer  = dynstring_create(g_alloc_heap, 2 * usize_kibibyte);
  ScriptBinder* scriptBinder = lsp_script_binder_create();
  JsonDoc*      jDoc         = json_create(g_alloc_heap, 1024);
  DynArray      openDocs     = dynarray_create_t(g_alloc_heap, LspDocument, 16);

  LspContext ctx = {
      .status       = LspStatus_Running,
      .readBuffer   = &readBuffer,
      .writeBuffer  = &writeBuffer,
      .scriptBinder = scriptBinder,
      .jDoc         = jDoc,
      .openDocs     = &openDocs,
      .in           = g_file_stdin,
      .out          = g_file_stdout,
  };

  while (LIKELY(ctx.status == LspStatus_Running)) {
    const LspHeader header  = lsp_read_header(&ctx);
    const String    content = lsp_read_sized(&ctx, header.contentLength);

    JsonResult jsonResult;
    json_read(jDoc, content, &jsonResult);
    if (UNLIKELY(jsonResult.type == JsonResultType_Fail)) {
      const String jsonErr = json_error_str(jsonResult.error);
      file_write_sync(g_file_stderr, fmt_write_scratch("lsp: Json-Error: {}\n", fmt_text(jsonErr)));
      ctx.status = LspStatus_ErrorInvalidJson;
      break;
    }

    lsp_handle_jrpc(&ctx, jsonResult.val);

    lsp_read_trim(&ctx);
    json_clear(jDoc);
  }

  script_binder_destroy(scriptBinder);
  json_destroy(jDoc);
  dynstring_destroy(&readBuffer);
  dynstring_destroy(&writeBuffer);

  dynarray_for_t(&openDocs, LspDocument, doc) { lsp_doc_destroy(doc); };
  dynarray_destroy(&openDocs);

  if (ctx.status != LspStatus_Exit) {
    const String errorMsg = g_lspStatusMessage[ctx.status];
    file_write_sync(g_file_stderr, fmt_write_scratch("lsp: {}\n", fmt_text(errorMsg)));
    return 1;
  }
  return 0;
}

static CliId g_optHelp, g_stdioFlag;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo Script Language Server"));

  g_stdioFlag = cli_register_flag(app, 0, string_lit("stdio"), CliOptionFlags_None);
  cli_register_desc(app, g_stdioFlag, string_lit("Use stdin and stdout for communication."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_stdioFlag);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_file_stdout);
    return 0;
  }

  if (cli_parse_provided(invoc, g_stdioFlag)) {
    return lsp_run_stdio();
  }

  file_write_sync(g_file_stderr, string_lit("lsp: No communication method specified.\n"));
  return 1;
}
