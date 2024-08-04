#include "core_diag.h"
#include "core_format.h"
#include "core_unicode.h"
#include "json_write.h"

#define json_compact_array_max_elems 4
#define json_compact_object_max_fields 4

typedef struct {
  const JsonDoc*       doc;
  const JsonWriteOpts* opts;
  u32                  indent;
} JsonWriteState;

static void json_state_write_string(JsonWriteState*, DynString*, String);
static void json_state_write(JsonWriteState*, DynString*, JsonVal);

static void json_state_write_separator(JsonWriteState* s, const JsonWriteMode m, DynString* str) {
  switch (m) {
  case JsonWriteMode_Minimal:
    break;
  case JsonWriteMode_Compact:
    dynstring_append_char(str, ' ');
    break;
  case JsonWriteMode_Verbose:
    dynstring_append(str, s->opts->newline);
    for (usize i = 0; i != s->indent; ++i) {
      dynstring_append(str, s->opts->indent);
    }
    break;
  }
}

static void json_state_write_indent(JsonWriteState* s, const JsonWriteMode m, DynString* str) {
  ++s->indent;
  json_state_write_separator(s, m, str);
}

static void json_state_write_outdent(JsonWriteState* s, const JsonWriteMode m, DynString* str) {
  --s->indent;
  json_state_write_separator(s, m, str);
}

static bool json_state_array_is_compact(JsonWriteState* s, const JsonVal val) {
  if (json_elem_count(s->doc, val) > json_compact_array_max_elems) {
    return false;
  }
  json_for_elems(s->doc, val, elem) {
    const JsonType elemType = json_type(s->doc, elem);
    if (elemType == JsonType_Object || elemType == JsonType_Array) {
      return false;
    }
  }
  return true;
}

static void json_state_write_array(JsonWriteState* s, DynString* str, const JsonVal val) {
  dynstring_append_char(str, '[');

  if (json_elem_count(s->doc, val) == 0) {
    dynstring_append_char(str, ']');
    return;
  }

  JsonWriteMode mode;
  switch (s->opts->mode) {
  case JsonWriteMode_Minimal:
    mode = JsonWriteMode_Minimal;
    break;
  case JsonWriteMode_Compact:
    mode = json_state_array_is_compact(s, val) ? JsonWriteMode_Compact : JsonWriteMode_Verbose;
    break;
  case JsonWriteMode_Verbose:
    mode = JsonWriteMode_Verbose;
    break;
  }

  json_state_write_indent(s, mode, str);

  bool first = true;
  json_for_elems(s->doc, val, elem) {
    if (!first) {
      dynstring_append_char(str, ',');
      json_state_write_separator(s, mode, str);
    }
    first = false;
    json_state_write(s, str, elem);
  }

  json_state_write_outdent(s, mode, str);
  dynstring_append_char(str, ']');
}

static bool json_state_object_is_compact(JsonWriteState* s, const JsonVal val) {
  if (json_field_count(s->doc, val) > json_compact_object_max_fields) {
    return false;
  }
  json_for_fields(s->doc, val, field) {
    const JsonType fieldType = json_type(s->doc, field.value);
    if (fieldType == JsonType_Object || fieldType == JsonType_Array) {
      return false;
    }
  }
  return true;
}

static void json_state_write_object(JsonWriteState* s, DynString* str, const JsonVal val) {
  dynstring_append_char(str, '{');

  if (json_field_count(s->doc, val) == 0) {
    dynstring_append_char(str, '}');
    return;
  }

  JsonWriteMode mode;
  switch (s->opts->mode) {
  case JsonWriteMode_Minimal:
    mode = JsonWriteMode_Minimal;
    break;
  case JsonWriteMode_Compact:
    mode = json_state_object_is_compact(s, val) ? JsonWriteMode_Compact : JsonWriteMode_Verbose;
    break;
  case JsonWriteMode_Verbose:
    mode = JsonWriteMode_Verbose;
    break;
  }

  json_state_write_indent(s, mode, str);

  bool first = true;
  json_for_fields(s->doc, val, field) {
    if (!first) {
      dynstring_append_char(str, ',');
      json_state_write_separator(s, mode, str);
    }
    first = false;

    json_state_write_string(s, str, json_string(s->doc, field.name));

    const bool pretty = s->opts->mode != JsonWriteMode_Minimal;
    dynstring_append(str, pretty ? string_lit(": ") : string_lit(":"));

    json_state_write(s, str, field.value);
  }

  json_state_write_outdent(s, mode, str);
  dynstring_append_char(str, '}');
}

static void json_state_write_string(JsonWriteState* s, DynString* str, const String val) {
  (void)s;

  dynstring_append_char(str, '"');
  mem_for_u8(val, itr) {
    switch (*itr) {
    case Unicode_Escape:
      dynstring_append(str, string_lit("\\"));
      break;
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
    case '$':
      if (s->opts->flags & JsonWriteFlags_EscapeDollarSign) {
        dynstring_append(str, string_lit("\\$"));
      } else {
        dynstring_append_char(str, '$');
      }
      break;
    default:
      dynstring_append_char(str, *itr);
      break;
    }
  }
  dynstring_append_char(str, '"');
}

static void json_state_write_number(JsonWriteState* s, DynString* str, const f64 val) {
  (void)s;
  format_write_f64(
      str,
      val,
      &format_opts_float(
              .minDecDigits    = 0,
              .maxDecDigits    = s->opts->numberMaxDecDigits,
              .expThresholdPos = s->opts->numberExpThresholdPos,
              .expThresholdNeg = s->opts->numberExpThresholdNeg));
}

static void json_state_write_bool(JsonWriteState* s, DynString* str, const bool val) {
  (void)s;
  dynstring_append(str, val ? string_lit("true") : string_lit("false"));
}

static void json_state_write_null(JsonWriteState* s, DynString* str) {
  (void)s;
  dynstring_append(str, string_lit("null"));
}

static void json_state_write(JsonWriteState* s, DynString* str, const JsonVal val) {
  switch (json_type(s->doc, val)) {
  case JsonType_Array:
    json_state_write_array(s, str, val);
    return;
  case JsonType_Object:
    json_state_write_object(s, str, val);
    return;
  case JsonType_String:
    json_state_write_string(s, str, json_string(s->doc, val));
    return;
  case JsonType_Number:
    json_state_write_number(s, str, json_number(s->doc, val));
    return;
  case JsonType_Bool:
    json_state_write_bool(s, str, json_bool(s->doc, val));
    return;
  case JsonType_Null:
    json_state_write_null(s, str);
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
