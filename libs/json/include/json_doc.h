#pragma once
#include "core_string.h"

/**
 * Definition for a Json Document.
 * Aims for compatiblity with rfc7159 json (https://datatracker.ietf.org/doc/html/rfc7159).
 */
typedef struct sJsonDoc JsonDoc;

typedef enum {
  JsonDocFlags_None        = 0,
  JsonDocFlags_NoStringDup = 1 << 0, // Do not duplicate strings when storing them in the document.
} JsonDocFlags;

/**
 * Type of a json value.
 */
typedef enum {
  JsonType_Array,
  JsonType_Object,
  JsonType_String,
  JsonType_Number,
  JsonType_Bool,
  JsonType_Null,

  JsonType_Count,
} JsonType;

/**
 * Parent kind of a json value.
 */
typedef enum {
  JsonParent_None,
  JsonParent_Array,
  JsonParent_Object,
} JsonParent;

/**
 * Handle to a Json value.
 */
typedef u32 JsonVal;

/**
 * Iterator for iterating object fields.
 */
typedef struct {
  String  name;  // 'string_empty' when no field was found.
  JsonVal value; // 'sentinel_u32' when no field was found.
} JsonFieldItr;

// clang-format off

/**
 * Iterate over all elements in an array value.
 *
 * Pre-condition: _ARRAY_ is a value of type JsonType_Array in the given document.
 */
#define json_for_elems(_DOC_, _ARRAY_, _VAR_, ...)                                                 \
  {                                                                                                \
    for (JsonVal _VAR_ = json_elem_begin(_DOC_, _ARRAY_); !sentinel_check(_VAR_);                  \
                 _VAR_ = json_elem_next(_DOC_, _VAR_)) {                                           \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

/**
 * Iterate over all fields in an object value.
 *
 * Pre-condition: _OBJECT_ is a value of type JsonType_Object in the given document.
 */
#define json_for_fields(_DOC_, _OBJECT_, _VAR_, ...)                                               \
  {                                                                                                \
    for (JsonFieldItr _VAR_ = json_field_begin(_DOC_, _OBJECT_); !sentinel_check(_VAR_.value);     \
                      _VAR_ = json_field_next(_DOC_, _VAR_.value)) {                               \
      __VA_ARGS__                                                                                  \
    }                                                                                              \
  }

/**
 * Add a literal string to the json document.
 */
#define json_add_string_lit(_DOC_, _STRING_LIT_) json_add_string(_DOC_, string_lit(_STRING_LIT_))

// clang-format on

/**
 * Create a new Json document.
 * NOTE: 'valueCapacity' is only the initial capacity, more space is automatically allocated when
 * required. Capacity of 0 is legal and will allocate memory when the first value is added.
 *
 * Should be destroyed using 'json_destroy()'.
 */
JsonDoc* json_create(Allocator*, usize valueCapacity, JsonDocFlags);

/**
 * Destroy a Json document.
 */
void json_destroy(JsonDoc*);

/**
 * Add a new array to the document.
 */
JsonVal json_add_array(JsonDoc*);

/**
 * Add a new object to the document.
 */
JsonVal json_add_object(JsonDoc*);

/**
 * Add a new string to the document.
 */
JsonVal json_add_string(JsonDoc*, String);

/**
 * Add a new number to the document.
 */
JsonVal json_add_number(JsonDoc*, f64);

/**
 * Add a new bool to the document.
 */
JsonVal json_add_bool(JsonDoc*, bool);

/**
 * Add a new null to the document.
 */
JsonVal json_add_null(JsonDoc*);

/**
 * Add a new element to an array.
 *
 * Pre-condition: array is a value of type JsonType_Array in the given document.
 * Pre-condition: elem is valid in the given document.
 * Pre-condition: elem doesn't have a parent yet.
 * Pre-condition: Adding elem to array does not create cycles.
 */
void json_add_elem(JsonDoc*, JsonVal array, JsonVal elem);

/**
 * Add a new field to an object.
 * Returns 'false' if the object already contains a field with the given name.
 * NOTE: When 'false' is returned the state of the object is not modified.
 *
 * Pre-condition: object is a value of type JsonType_Object in the given document.
 * Pre-condition: name is a value of type JsonType_String in the given document.
 * Pre-condition: name doesn't have a parent yet.
 * Pre-condition: string value of name is not empty.
 * Pre-condition: val is valid in the given document.
 * Pre-condition: val doesn't have a parent yet.
 * Pre-condition: Adding val to object does not create cycles.
 */
bool json_add_field(JsonDoc*, JsonVal object, JsonVal name, JsonVal val);

/**
 * Add a new field to an object.
 * Returns 'false' if the object already contains a field with the given name.
 * NOTE: When 'false' is returned the state of the object is not modified.
 *
 * Pre-condition: object is a value of type JsonType_Object in the given document.
 * Pre-condition: !string_is_empty(name).
 * Pre-condition: val is valid in the given document.
 * Pre-condition: val doesn't have a parent yet.
 * Pre-condition: Adding val to object does not create cycles.
 */
bool json_add_field_str(JsonDoc*, JsonVal object, String name, JsonVal val);

/**
 * Retrieve a textual represention of a json type.
 */
String json_type_str(JsonType);

/**
 * Retrieve the type of a value.
 *
 * Pre-condition: JsonVal is valid in the given document.
 */
JsonType json_type(const JsonDoc*, JsonVal);

/**
 * Retrieve the parent kind of a value.
 *
 * Pre-condition: JsonVal is valid in the given document.
 */
JsonParent json_parent(const JsonDoc*, JsonVal);

/**
 * Lookup an element by its index.
 * Returns 'sentinel_u32' when there is no element at the given index.
 *
 * Pre-condition: array is a value of type JsonType_Array in the given document.
 */
JsonVal json_elem(const JsonDoc*, JsonVal array, u32 idx);

/**
 * Retrieve the amount of elements in an array.
 *
 * Pre-condition: array is a value of type JsonType_Array in the given document.
 */
u32 json_elem_count(const JsonDoc*, JsonVal array);

/**
 * Retrieve the first element in an array.
 * Returns 'sentinel_u32' when the array has no elements.
 *
 * Pre-condition: array is a value of type JsonType_Array in the given document.
 */
JsonVal json_elem_begin(const JsonDoc*, JsonVal array);

/**
 * Retrieve the next element in an array.
 * Returns 'sentinel_u32' when there are no more elements.
 *
 * Pre-condition: elem is a value with a parent kind of JsonParent_Array.
 */
JsonVal json_elem_next(const JsonDoc*, JsonVal elem);

/**
 * Lookup a object field by its name.
 * Returns 'sentinel_u32' when no field was found with the given name.
 *
 * Pre-condition: object is a value of type JsonType_Object in the given document.
 */
JsonVal json_field(const JsonDoc*, JsonVal object, String name);

/**
 * Retrieve the amount of fields in an object.
 *
 * Pre-condition: object is a value of type JsonType_Object in the given document.
 */
u32 json_field_count(const JsonDoc*, JsonVal object);

/**
 * Retrieve the first field in an object.
 * Returns an iterator with a value of 'sentinel_u32' when the object has no fields.
 *
 * Pre-condition: object is a value of type JsonType_Object in the given document.
 */
JsonFieldItr json_field_begin(const JsonDoc*, JsonVal object);

/**
 * Retrieve the next field in an object.
 * Returns an iterator with a value of 'sentinel_u32' when there are no more fields
 *
 * Pre-condition: fieldVal is a value with a parent kind of JsonParent_Object.
 */
JsonFieldItr json_field_next(const JsonDoc*, JsonVal fieldVal);

/**
 * Retrieve the value of a string.
 *
 * Pre-condition: JsonVal is a value of type JsonType_String in the given document.
 */
String json_string(const JsonDoc*, JsonVal);

/**
 * Retrieve the value of a number.
 *
 * Pre-condition: JsonVal is a value of type JsonType_Number in the given document.
 */
f64 json_number(const JsonDoc*, JsonVal);

/**
 * Retrieve the value of a bool.
 *
 * Pre-condition: JsonVal is a value of type JsonType_Bool in the given document.
 */
bool json_bool(const JsonDoc*, JsonVal);
