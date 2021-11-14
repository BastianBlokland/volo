#include "core_diag.h"
#include "core_format.h"
#include "json_write.h"

typedef struct {
  const JsonDoc*       doc;
  const JsonWriteOpts* opts;
  u32                  indent;
} JsonWriteState;

static void json_state_write_string(JsonWriteState*, DynString*, String);
static void json_state_write(JsonWriteState*, DynString*, JsonVal);

static void json_state_write_newline(JsonWriteState* state, DynString* str) {
  if (state->opts->flags & JsonWriteFlags_Pretty) {
    dynstring_append(str, state->opts->newline);
    for (usize i = 0; i != state->indent; ++i) {
      dynstring_append(str, state->opts->indent);
    }
  }
}

static void json_state_write_indent(JsonWriteState* state, DynString* str) {
  ++state->indent;
  json_state_write_newline(state, str);
}

static void json_state_write_outdent(JsonWriteState* state, DynString* str) {
  --state->indent;
  json_state_write_newline(state, str);
}

static void json_state_write_array(JsonWriteState* state, DynString* str, const JsonVal val) {
  dynstring_append_char(str, '[');

  if (json_elem_count(state->doc, val) == 0) {
    dynstring_append_char(str, ']');
    return;
  }

  json_state_write_indent(state, str);

  bool first = true;
  json_for_elems(state->doc, val, elem, {
    if (!first) {
      dynstring_append_char(str, ',');
      json_state_write_newline(state, str);
    }
    first = false;
    json_state_write(state, str, elem);
  });

  json_state_write_outdent(state, str);
  dynstring_append_char(str, ']');
}

static void json_state_write_object(JsonWriteState* state, DynString* str, const JsonVal val) {
  dynstring_append_char(str, '{');

  if (json_field_count(state->doc, val) == 0) {
    dynstring_append_char(str, '}');
    return;
  }

  json_state_write_indent(state, str);

  bool first = true;
  json_for_fields(state->doc, val, field, {
    if (!first) {
      dynstring_append_char(str, ',');
      json_state_write_newline(state, str);
    }
    first = false;

    json_state_write_string(state, str, field.name);

    const bool pretty = (state->opts->flags & JsonWriteFlags_Pretty) != 0;
    dynstring_append(str, pretty ? string_lit(": ") : string_lit(":"));

    json_state_write(state, str, field.value);
  });

  json_state_write_outdent(state, str);
  dynstring_append_char(str, '}');
}

static void json_state_write_string(JsonWriteState* state, DynString* str, const String val) {
  (void)state;

  dynstring_append_char(str, '"');
  mem_for_u8(val, ch, {
    switch (ch) {
    case '"':
      dynstring_append(str, string_lit("\\\""));
      break;
    case '\\':
      dynstring_append(str, string_lit("\\\\"));
      break;
    case '\b':
      dynstring_append(str, string_lit("\\b"));
      break;
    case '\f':
      dynstring_append(str, string_lit("\\f"));
      break;
    case '\n':
      dynstring_append(str, string_lit("\\n"));
      break;
    case '\r':
      dynstring_append(str, string_lit("\\r"));
      break;
    case '\t':
      dynstring_append(str, string_lit("\\t"));
      break;
    default:
      dynstring_append_char(str, ch);
      break;
    }
  });
  dynstring_append_char(str, '"');
}

static void json_state_write_number(JsonWriteState* state, DynString* str, const f64 val) {
  (void)state;
  format_write_f64(
      str,
      val,
      &format_opts_float(
              .minDecDigits    = 0,
              .maxDecDigits    = 10,
              .expThresholdPos = 1e10,
              .expThresholdNeg = 1e-10));
}

static void json_state_write_bool(JsonWriteState* state, DynString* str, const bool val) {
  (void)state;
  dynstring_append(str, val ? string_lit("true") : string_lit("false"));
}

static void json_state_write_null(JsonWriteState* state, DynString* str) {
  (void)state;
  dynstring_append(str, string_lit("null"));
}

static void json_state_write(JsonWriteState* state, DynString* str, const JsonVal val) {
  switch (json_type(state->doc, val)) {
  case JsonType_Array:
    json_state_write_array(state, str, val);
    return;
  case JsonType_Object:
    json_state_write_object(state, str, val);
    return;
  case JsonType_String:
    json_state_write_string(state, str, json_string(state->doc, val));
    return;
  case JsonType_Number:
    json_state_write_number(state, str, json_number(state->doc, val));
    return;
  case JsonType_Bool:
    json_state_write_bool(state, str, json_bool(state->doc, val));
    return;
  case JsonType_Null:
    json_state_write_null(state, str);
    return;
  case JsonType_Count:
    break;
  }
  diag_crash();
}

void json_write(DynString* str, const JsonDoc* doc, const JsonVal val, const JsonWriteOpts* opts) {
  JsonWriteState state = {
      .doc    = doc,
      .opts   = opts,
      .indent = 0,
  };
  json_state_write(&state, str, val);
}
