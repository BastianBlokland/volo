#include "core_array.h"
#include "core_diag.h"
#include "core_stringtable.h"
#include "json_read.h"

#include "lex_internal.h"

#define json_depth_max 100

typedef struct {
  JsonDoc*      doc;
  JsonReadFlags flags;
} JsonReadState;

#define json_err(_ERR_)                                                                            \
  (JsonResult) { .type = JsonResultType_Fail, .error = (_ERR_) }

#define json_success(_VAL_)                                                                        \
  (JsonResult) { .type = JsonResultType_Success, .val = (_VAL_) }

static const String g_errorStrs[] = {
    string_static("DuplicateField"),
    string_static("InvalidChar"),
    string_static("InvalidCharInFalse"),
    string_static("InvalidCharInNull"),
    string_static("InvalidCharInString"),
    string_static("InvalidCharInTrue"),
    string_static("InvalidEscapeSequence"),
    string_static("InvalidFieldName"),
    string_static("InvalidFieldSeparator"),
    string_static("MaximumDepthExceeded"),
    string_static("TooLongString"),
    string_static("Truncated"),
    string_static("UnexpectedToken"),
    string_static("UnterminatedString"),
};

ASSERT(array_elems(g_errorStrs) == JsonError_Count, "Incorrect number of JsonError strings");

String json_error_str(const JsonError error) {
  diag_assert(error < JsonError_Count);
  return g_errorStrs[error];
}

static String json_read_internal(JsonReadState*, String, JsonResult*);
static String json_read_with_start_token(JsonReadState*, String, JsonToken, JsonResult*);

static String json_read_array(JsonReadState* state, String input, JsonResult* res) {
  const JsonVal array = json_add_array(state->doc);

  JsonToken  token;
  JsonResult valRes;
  while (true) {
    // Read value.
    input = json_lex(input, &token);
    if (token.type == JsonTokenType_BracketClose) {
      // NOTE: Not fully spec compliant but we accept arrays with trailing comma's.
      goto Success;
    }
    input = json_read_with_start_token(state, input, token, &valRes);
    if (valRes.type == JsonResultType_Fail) {
      *res = json_err(valRes.error);
      return input;
    }
    json_add_elem(state->doc, array, valRes.val);

    // Read separator (comma).
    input = json_lex(input, &token);
    switch (token.type) {
    case JsonTokenType_BracketClose:
      goto Success;
    case JsonTokenType_Comma:
      break;
    case JsonTokenType_End:
      *res = json_err(JsonError_Truncated);
      break;
    case JsonTokenType_Error:
      *res = json_err(token.val_error);
      break;
    default:
      *res = json_err(JsonError_UnexpectedToken);
      return input;
    }
  }
Success:
  *res = json_success(array);
  return input;
}

static String json_read_object(JsonReadState* state, String input, JsonResult* res) {
  const JsonVal object = json_add_object(state->doc);

  JsonToken  token;
  JsonResult valRes;
  while (true) {
    // Read field name.
    input = json_lex(input, &token);
    if (token.type == JsonTokenType_CurlyClose) {
      // NOTE: Not fully spec compliant but we accept objects with trailing comma's.
      goto Success;
    }
    if (token.type != JsonTokenType_String || string_is_empty(token.val_string)) {
      *res = json_err(
          token.type == JsonTokenType_End ? JsonError_Truncated : JsonError_InvalidFieldName);
      return input;
    }
    JsonVal fieldName;
    if (state->flags & JsonReadFlags_HashOnlyFieldNames) {
      StringHash fieldNameHash;
#ifndef VOLO_FAST
      fieldNameHash = stringtable_add(g_stringtable, token.val_string);
#else
      fieldNameHash = string_hash(token.val_string);
#endif
      fieldName = json_add_string_hash(state->doc, fieldNameHash);
    } else {
      fieldName = json_add_string(state->doc, token.val_string);
    }

    // Read separator (colon).
    input = json_lex(input, &token);
    if (token.type != JsonTokenType_Colon) {
      *res = json_err(JsonError_InvalidFieldSeparator);
      return input;
    }

    // Read field value.
    input = json_read_internal(state, input, &valRes);
    if (valRes.type == JsonResultType_Fail) {
      *res = json_err(valRes.error);
      return input;
    }
    if (!json_add_field(state->doc, object, fieldName, valRes.val)) {
      *res = json_err(JsonError_DuplicateField);
      return input;
    }

    // Read separator (comma).
    input = json_lex(input, &token);
    switch (token.type) {
    case JsonTokenType_CurlyClose:
      goto Success;
    case JsonTokenType_Comma:
      break;
    case JsonTokenType_End:
      *res = json_err(JsonError_Truncated);
      break;
    case JsonTokenType_Error:
      *res = json_err(token.val_error);
      break;
    default:
      *res = json_err(JsonError_UnexpectedToken);
      return input;
    }
  }
Success:
  *res = json_success(object);
  return input;
}

static String json_read_with_start_token(
    JsonReadState* state, String input, JsonToken startToken, JsonResult* res) {

  static THREAD_LOCAL u32 depth;
  if (++depth > json_depth_max) {
    *res = json_err(JsonError_MaximumDepthExceeded);
    --depth;
    return input;
  }

  switch (startToken.type) {
  case JsonTokenType_BracketOpen:
    input = json_read_array(state, input, res);
    break;
  case JsonTokenType_CurlyOpen:
    input = json_read_object(state, input, res);
    break;
  case JsonTokenType_BracketClose:
  case JsonTokenType_CurlyClose:
  case JsonTokenType_Comma:
  case JsonTokenType_Colon:
    *res = json_err(JsonError_UnexpectedToken);
    break;
  case JsonTokenType_String:
    *res = json_success(json_add_string(state->doc, startToken.val_string));
    break;
  case JsonTokenType_Number:
    *res = json_success(json_add_number(state->doc, startToken.val_number));
    break;
  case JsonTokenType_True:
    *res = json_success(json_add_bool(state->doc, true));
    break;
  case JsonTokenType_False:
    *res = json_success(json_add_bool(state->doc, false));
    break;
  case JsonTokenType_Null:
    *res = json_success(json_add_null(state->doc));
    break;
  case JsonTokenType_Error:
    *res = json_err(startToken.val_error);
    break;
  case JsonTokenType_End:
    *res = json_err(JsonError_Truncated);
    break;
  }

  --depth;
  return input;
}

static String json_read_internal(JsonReadState* state, String input, JsonResult* res) {
  JsonToken startToken;
  input = json_lex(input, &startToken);
  return json_read_with_start_token(state, input, startToken, res);
}

String json_read(JsonDoc* doc, const String input, const JsonReadFlags flags, JsonResult* res) {
  JsonReadState state = {
      .doc   = doc,
      .flags = flags,
  };
  return json_read_internal(&state, input, res);
}
