#include "core_diag.h"
#include "json_doc.h"
#include "json_eq.h"

static bool json_eq_array(JsonDoc* doc, JsonVal x, JsonVal y) {
  const usize count = json_elem_count(doc, x);
  if (count != json_elem_count(doc, y)) {
    return false;
  }
  if (count == 0) {
    return true;
  }
  JsonVal xElem = json_elem_begin(doc, x);
  JsonVal yElem = json_elem_begin(doc, y);
  do {
    if (!json_eq(doc, xElem, yElem)) {
      return false;
    }
    xElem = json_elem_next(doc, xElem);
    yElem = json_elem_next(doc, yElem);
  } while (!sentinel_check(xElem));

  return true;
}

static bool json_eq_object(JsonDoc* doc, JsonVal x, JsonVal y) {
  const usize count = json_field_count(doc, x);
  if (count != json_field_count(doc, y)) {
    return false;
  }
  if (count == 0) {
    return true;
  }

  /**
   * TODO: Consider if we want to threat different order of fields as equal.
   * At the moment the doc, parser and writer all preserve field order and thus we can rely on the
   * order here.
   * Json itself however specifies objects as an unordered collection of fields.
   * More info: https://datatracker.ietf.org/doc/html/rfc7159#section-4
   */

  JsonFieldItr xFieldItr = json_field_begin(doc, x);
  JsonFieldItr yFieldItr = json_field_begin(doc, y);
  do {
    if (json_string_hash(doc, xFieldItr.name) != json_string_hash(doc, yFieldItr.name)) {
      return false;
    }
    if (!json_eq(doc, xFieldItr.value, yFieldItr.value)) {
      return false;
    }
    xFieldItr = json_field_next(doc, xFieldItr.value);
    yFieldItr = json_field_next(doc, yFieldItr.value);
  } while (!sentinel_check(xFieldItr.value));

  return true;
}

bool json_eq(JsonDoc* doc, JsonVal x, JsonVal y) {
  const JsonType type = json_type(doc, x);
  if (type != json_type(doc, y)) {
    return false;
  }
  switch (type) {
  case JsonType_Array:
    return json_eq_array(doc, x, y);
  case JsonType_Object:
    return json_eq_object(doc, x, y);
  case JsonType_String:
    return json_string_hash(doc, x) == json_string_hash(doc, y);
  case JsonType_Number:
    return json_number(doc, x) == json_number(doc, y); // TODO: Should we add a diff threshold?
  case JsonType_Bool:
    return json_bool(doc, x) == json_bool(doc, y);
  case JsonType_Null:
    return true;
  case JsonType_Count:
    break;
  }
  diag_crash();
}
