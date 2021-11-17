#pragma once
#include "data_type.h"

typedef enum {
  DataContainer_None,
  DataContainer_Pointer,
  DataContainer_Array,
} DataContainer;

/**
 * Meta information for a data value.
 * Combination of a type and properties of a specific instance (for example if its a pointer).
 */
typedef struct {
  DataType      type;
  DataContainer container;
} DataMeta;

/**
 * Construct Meta information for a data value.
 */
#define data_meta_t(_TYPE_, ...)                                                                   \
  (DataMeta) { .type = _TYPE_, ##__VA_ARGS__ }

/**
 * Lookup a primitive data type.
 */
#define data_prim_t(_PRIM_) data_prim(DataPrim_##_PRIM_)

DataType data_prim(DataPrim);

/**
 * Retrieve the name of a registered type.
 */
String data_name(DataType);

/**
 * Retrieve the size (in bytes) of a registered type.
 */
usize data_size(DataType);

/**
 * Retrieve the alignment requirement (in bytes) of a registered type.
 */
usize data_align(DataType);

/**
 * Get the size (in bytes) that a value with the given DataMeta occupies.
 */
usize data_meta_size(DataMeta);

/**
 * Register a new Struct type.
 *
 * Pre-condition: No type with the same name has been registered.
 */
#define data_register_struct_t(_T_)                                                                \
  MAYBE_UNUSED const DataType t_##_T_ =                                                            \
      data_register_struct(string_lit(#_T_), sizeof(_T_), alignof(_T_))

DataType data_register_struct(String name, usize size, usize align);

/**
 * Register a new field for a Struct,
 *
 * Pre-condition: parent is a Struct.
 */
#define data_register_field_t(_T_, _FIELD_, _TYPE_, ...)                                           \
  data_register_field(                                                                             \
      t_##_T_, string_lit(#_FIELD_), offsetof(_T_, _FIELD_), data_meta_t(_TYPE_, __VA_ARGS__));

void data_register_field(DataType parent, String name, usize offset, DataMeta);

/**
 * Register a new Enum type.
 *
 * Pre-condition: No type with the same name has been registered.
 */
#define data_register_enum_t(_T_)                                                                  \
  MAYBE_UNUSED const DataType t_##_T_ = data_register_enum(string_lit(#_T_))

DataType data_register_enum(String name);

/**
 * Register a new constant for an Enum,
 *
 * Pre-condition: parent is an Enum.
 */
#define data_register_const_t(_T_, _ENTRY_)                                                        \
  data_register_const(t_##_T_, string_lit(#_ENTRY_), _T_##_##_ENTRY_);

void data_register_const(DataType parent, String name, i32 value);
