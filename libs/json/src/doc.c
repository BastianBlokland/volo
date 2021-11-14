#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "json_doc.h"

typedef struct {
  JsonVal elemHead, elemTail;
  u32     elemCount;
} JsonArrayData;

typedef struct {
  JsonVal fieldHead;
  u32     fieldCount;
} JsonObjectData;

typedef struct {
  u32     typeAndParent;
  JsonVal next;
  union {
    JsonArrayData  val_array;
    JsonObjectData val_object;
    String         val_string;
    f64            val_number;
    bool           val_bool;
  };
} JsonValData;

struct sJsonDoc {
  DynArray   values; // JsonValData[]
  Allocator* alloc;
};

static JsonValData* json_val_data(const JsonDoc* doc, const JsonVal val) {
  diag_assert_msg(val < doc->values.size, "Out of bounds JsonVal");
  return dynarray_at_t(&doc->values, val, JsonValData);
}

static JsonVal json_add_data(JsonDoc* doc, JsonValData data) {
  const JsonVal val                           = (JsonVal)doc->values.size;
  *dynarray_push_t(&doc->values, JsonValData) = data;
  return val;
}

JsonDoc* json_create(Allocator* alloc, usize valueCapacity) {
  JsonDoc* doc = alloc_alloc_t(alloc, JsonDoc);
  *doc         = (JsonDoc){
      .values = dynarray_create_t(alloc, JsonValData, valueCapacity),
      .alloc  = alloc,
  };
  return doc;
}

void json_destroy(JsonDoc* doc) {
  dynarray_for_t(&doc->values, JsonValData, data, {
    switch (data->typeAndParent & 0xFFFF) {
    case JsonType_String:
      if (data->val_string.ptr) {
        string_free(doc->alloc, data->val_string);
      }
      break;
    default:
      break;
    }
  });

  dynarray_destroy(&doc->values);

  alloc_free_t(doc->alloc, doc);
}

JsonVal json_add_array(JsonDoc* doc) {
  return json_add_data(
      doc,
      (JsonValData){
          .typeAndParent = JsonType_Array,
          .next          = sentinel_u32,
          .val_array     = {.elemHead = sentinel_u32, .elemTail = sentinel_u32, .elemCount = 0},
      });
}

JsonVal json_add_object(JsonDoc* doc) {
  return json_add_data(
      doc,
      (JsonValData){
          .typeAndParent = JsonType_Object,
          .next          = sentinel_u32,
          .val_object    = {.fieldHead = sentinel_u32, .fieldCount = 0},
      });
}

JsonVal json_add_string(JsonDoc* doc, const String string) {
  return json_add_data(
      doc,
      (JsonValData){
          .typeAndParent = JsonType_String,
          .next          = sentinel_u32,
          .val_string    = string_is_empty(string) ? string_empty : string_dup(doc->alloc, string),
      });
}

JsonVal json_add_number(JsonDoc* doc, const f64 number) {
  return json_add_data(
      doc,
      (JsonValData){
          .typeAndParent = JsonType_Number,
          .next          = sentinel_u32,
          .val_number    = number,
      });
}

JsonVal json_add_bool(JsonDoc* doc, const bool boolean) {
  return json_add_data(
      doc,
      (JsonValData){
          .typeAndParent = JsonType_Bool,
          .next          = sentinel_u32,
          .val_bool      = boolean,
      });
}

JsonVal json_add_null(JsonDoc* doc) {
  return json_add_data(
      doc,
      (JsonValData){
          .typeAndParent = JsonType_Null,
          .next          = sentinel_u32,
      });
}

void json_add_elem(JsonDoc* doc, const JsonVal array, const JsonVal elem) {
  diag_assert_msg(json_parent(doc, elem) == JsonParent_None, "Given value is already parented");
  diag_assert_msg(json_type(doc, array) == JsonType_Array, "Invalid array value");
  diag_assert_msg(array != elem, "Arrays cannot contain cycles"); // TODO: Detect indirect cycles.

  JsonValData* arrayData = json_val_data(doc, array);
  JsonValData* elemData  = json_val_data(doc, elem);

  // Add the element to the end of the array linked-list.
  if (sentinel_check(arrayData->val_array.elemTail)) {
    arrayData->val_array.elemHead = elem;
    arrayData->val_array.elemTail = elem;
  } else {
    json_val_data(doc, arrayData->val_array.elemTail)->next = elem;
    arrayData->val_array.elemTail                           = elem;
  }

  elemData->typeAndParent |= (u32)JsonParent_Array << 16;
  ++arrayData->val_array.elemCount;
}

bool json_add_field(JsonDoc* doc, const JsonVal object, const JsonVal name, const JsonVal val) {
  diag_assert_msg(json_type(doc, object) == JsonType_Object, "Invalid object value");
  diag_assert_msg(
      object != name && object != val,
      "Objects cannot contain cycles"); // TODO: Detect indirect cycles.
  diag_assert_msg(json_parent(doc, name) == JsonParent_None, "Given name is already parented");
  diag_assert_msg(!string_is_empty(json_string(doc, name)), "Field name cannot be empty");
  diag_assert_msg(json_parent(doc, val) == JsonParent_None, "Given value is already parented");

  const String nameStr    = json_string(doc, name);
  JsonValData* objectData = json_val_data(doc, object);

  // Walk the linked-list of fields to check for duplicate names and to find the last link.
  JsonVal* link = &objectData->val_object.fieldHead;
  while (!sentinel_check(*link)) {
    const JsonValData* nameValData = json_val_data(doc, *link);
    if (string_eq(nameValData->val_string, nameStr)) {
      // Existing field found with the same name.
      return false;
    }
    link = &json_val_data(doc, nameValData->next)->next;
  }

  *link = name;
  ++objectData->val_object.fieldCount;
  json_val_data(doc, name)->next = val;
  json_val_data(doc, name)->typeAndParent |= (u32)JsonParent_Object << 16;
  json_val_data(doc, val)->typeAndParent |= (u32)JsonParent_Object << 16;
  return true;
}

bool json_add_field_str(JsonDoc* doc, const JsonVal object, const String name, const JsonVal val) {
  return json_add_field(doc, object, json_add_string(doc, name), val);
}

String json_type_str(const JsonType type) {
  static const String names[] = {
      string_static("array"),
      string_static("object"),
      string_static("string"),
      string_static("number"),
      string_static("bool"),
      string_static("null"),
  };
  ASSERT(array_elems(names) == JsonType_Count, "Incorrect number of json-type names");
  return names[type];
}

JsonType json_type(const JsonDoc* doc, const JsonVal val) {
  return (JsonType)(json_val_data(doc, val)->typeAndParent & 0xFFFF);
}

JsonParent json_parent(const JsonDoc* doc, const JsonVal val) {
  return (JsonParent)(json_val_data(doc, val)->typeAndParent >> 16);
}

JsonVal json_elem(const JsonDoc* doc, const JsonVal array, const u32 idx) {
  diag_assert_msg(json_type(doc, array) == JsonType_Array, "Invalid array value");

  JsonVal link = json_val_data(doc, array)->val_array.elemHead;
  for (u32 i = 0; !sentinel_check(link); ++i) {
    if (i == idx) {
      return link;
    }
    link = json_val_data(doc, link)->next;
  }

  return sentinel_u32;
}

u32 json_elem_count(const JsonDoc* doc, const JsonVal array) {
  diag_assert_msg(json_type(doc, array) == JsonType_Array, "Invalid array value");
  return json_val_data(doc, array)->val_array.elemCount;
}

JsonVal json_elem_begin(const JsonDoc* doc, const JsonVal array) {
  diag_assert_msg(json_type(doc, array) == JsonType_Array, "Invalid array value");
  return json_val_data(doc, array)->val_array.elemHead;
}

JsonVal json_elem_next(const JsonDoc* doc, const JsonVal elem) {
  diag_assert_msg(json_parent(doc, elem) == JsonParent_Array, "Invalid array elem");
  return json_val_data(doc, elem)->next;
}

JsonVal json_field(const JsonDoc* doc, const JsonVal object, const String name) {
  diag_assert_msg(json_type(doc, object) == JsonType_Object, "Invalid object value");

  JsonValData* objectData = json_val_data(doc, object);

  // Search the linked-list for a entry that matches the given name.
  JsonVal link = objectData->val_object.fieldHead;
  while (!sentinel_check(link)) {
    const JsonValData* nameValData = json_val_data(doc, link);
    if (string_eq(nameValData->val_string, name)) {
      return nameValData->next;
    }
    link = json_val_data(doc, nameValData->next)->next;
  }

  // Not found.
  return sentinel_u32;
}

u32 json_field_count(const JsonDoc* doc, const JsonVal object) {
  diag_assert_msg(json_type(doc, object) == JsonType_Object, "Invalid object value");
  return json_val_data(doc, object)->val_object.fieldCount;
}

JsonFieldItr json_field_begin(const JsonDoc* doc, const JsonVal object) {
  diag_assert_msg(json_type(doc, object) == JsonType_Object, "Invalid object value");

  JsonValData* objectData = json_val_data(doc, object);
  if (sentinel_check(objectData->val_object.fieldHead)) {
    return (JsonFieldItr){.name = string_empty, .value = sentinel_u32};
  }
  JsonValData* nameData = json_val_data(doc, objectData->val_object.fieldHead);
  return (JsonFieldItr){.name = nameData->val_string, .value = nameData->next};
}

JsonFieldItr json_field_next(const JsonDoc* doc, JsonVal fieldVal) {
  diag_assert_msg(json_parent(doc, fieldVal) == JsonParent_Object, "Invalid field value");

  JsonValData* itrValData = json_val_data(doc, fieldVal);
  if (sentinel_check(itrValData->next)) {
    return (JsonFieldItr){.name = string_empty, .value = sentinel_u32};
  }
  JsonValData* nameData = json_val_data(doc, itrValData->next);
  return (JsonFieldItr){.name = nameData->val_string, .value = nameData->next};
}

String json_string(const JsonDoc* doc, const JsonVal val) {
  diag_assert_msg(json_type(doc, val) == JsonType_String, "Given JsonVal is not a string");
  return json_val_data(doc, val)->val_string;
}

f64 json_number(const JsonDoc* doc, const JsonVal val) {
  diag_assert_msg(json_type(doc, val) == JsonType_Number, "Given JsonVal is not a number");
  return json_val_data(doc, val)->val_number;
}

bool json_bool(const JsonDoc* doc, const JsonVal val) {
  diag_assert_msg(json_type(doc, val) == JsonType_Bool, "Given JsonVal is not a boolean");
  return json_val_data(doc, val)->val_bool;
}
