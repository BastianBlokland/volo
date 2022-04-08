#pragma once
#include "data_type.h"

/**
 * Data registry, container for data-type definitions.
 */
typedef struct sDataReg DataReg;

typedef enum {
  DataContainer_None,
  DataContainer_Pointer,
  DataContainer_Array,
} DataContainer;

typedef enum {
  DataFlags_None     = 0,
  DataFlags_Opt      = 1 << 0,
  DataFlags_NotEmpty = 1 << 1,
} DataFlags;

/**
 * Meta information for a data value.
 * Combination of a type and properties of a specific instance (for example if its a pointer).
 */
typedef struct {
  DataType      type;
  DataFlags     flags;
  DataContainer container;
} DataMeta;

#define data_meta_t(_DATA_TYPE_, ...) ((DataMeta){.type = _DATA_TYPE_, ##__VA_ARGS__})
#define data_prim_t(_PRIM_) ((DataType)DataKind_##_PRIM_)

/**
 * Create a new data registry.
 * Destroy using 'data_reg_destroy()'.
 */
DataReg* data_reg_create(Allocator*);

/**
 * Destroy a data registry.
 */
void data_reg_destroy(DataReg*);

/**
 * Retrieve the name of a registered type.
 */
String data_name(const DataReg*, DataType);

/**
 * Retrieve the size (in bytes) of a registered type.
 */
usize data_size(const DataReg*, DataType);

/**
 * Retrieve the alignment requirement (in bytes) of a registered type.
 */
usize data_align(const DataReg*, DataType);

/**
 * Get the size (in bytes) that a value with the given DataMeta occupies.
 */
usize data_meta_size(const DataReg*, DataMeta);

/**
 * Register a new Struct type.
 */
#define data_reg_struct_t(_REG_, _T_)                                                              \
  MAYBE_UNUSED const DataType t_##_T_ =                                                            \
      data_reg_struct((_REG_), string_lit(#_T_), sizeof(_T_), alignof(_T_))

DataType data_reg_struct(DataReg*, String name, usize size, usize align);

/**
 * Register a new field for a Struct,
 */
#define data_reg_field_t(_REG_, _PARENT_, _FIELD_, _DATA_TYPE_, ...)                               \
  data_reg_field(                                                                                  \
      (_REG_),                                                                                     \
      t_##_PARENT_,                                                                                \
      string_lit(#_FIELD_),                                                                        \
      offsetof(_PARENT_, _FIELD_),                                                                 \
      data_meta_t(_DATA_TYPE_, __VA_ARGS__));

void data_reg_field(DataReg*, DataType parent, String name, usize offset, DataMeta);

/**
 * Register a new Enum type.
 */
#define data_reg_enum_t(_REG_, _T_)                                                                \
  MAYBE_UNUSED const DataType t_##_T_ = data_reg_enum((_REG_), string_lit(#_T_))

DataType data_reg_enum(DataReg*, String name);

/**
 * Register a new constant for an Enum,
 */
#define data_reg_const_t(_REG_, _PARENT_, _ENTRY_)                                                 \
  data_reg_const((_REG_), t_##_PARENT_, string_lit(#_ENTRY_), _PARENT_##_##_ENTRY_);

#define data_reg_const_custom(_REG_, _PARENT_, _NAME_, _VALUE_)                                    \
  data_reg_const((_REG_), t_##_PARENT_, string_lit(#_NAME_), _VALUE_);

void data_reg_const(DataReg*, DataType parent, String name, i32 value);
