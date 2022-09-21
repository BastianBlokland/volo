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
 * Retrieve the comment attached to a registered type,
 * NOTE: Returns an empty string if no comment was registered for the type.
 */
String data_comment(const DataReg*, DataType);

/**
 * Get the size (in bytes) that a value with the given DataMeta occupies.
 */
usize data_meta_size(const DataReg*, DataMeta);

/**
 * Declare a type without defining it yet.
 * NOTE: The type needs to be defined (for example using 'data_reg_struct') before usage.
 */
#define data_declare_t(_REG_, _T_) data_declare((_REG_), string_lit(#_T_))

DataType data_declare(DataReg*, String name);

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
      data_meta_t(_DATA_TYPE_, __VA_ARGS__))

void data_reg_field(DataReg*, DataType parent, String name, usize offset, DataMeta);

/**
 * Register a new Union type.
 */
#define data_reg_union_t(_REG_, _T_, _TAG_FIELD_)                                                  \
  MAYBE_UNUSED const DataType t_##_T_ = data_reg_union(                                            \
      (_REG_), string_lit(#_T_), sizeof(_T_), alignof(_T_), offsetof(_T_, _TAG_FIELD_))

DataType data_reg_union(DataReg*, String name, usize size, usize align, usize tagOffset);

/**
 * Register a new choice for a Union,
 */
#define data_reg_choice_t(_REG_, _PARENT_, _TAG_, _FIELD_, _DATA_TYPE_, ...)                       \
  data_reg_choice(                                                                                 \
      (_REG_),                                                                                     \
      t_##_PARENT_,                                                                                \
      string_lit(#_TAG_),                                                                          \
      (_TAG_),                                                                                     \
      offsetof(_PARENT_, _FIELD_),                                                                 \
      data_meta_t(_DATA_TYPE_, __VA_ARGS__))

#define data_reg_choice_empty(_REG_, _PARENT_, _TAG_)                                              \
  data_reg_choice((_REG_), t_##_PARENT_, string_lit(#_TAG_), (_TAG_), 0, (DataMeta){0})

void data_reg_choice(DataReg*, DataType parent, String name, i32 tag, usize offset, DataMeta);

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
  data_reg_const((_REG_), t_##_PARENT_, string_lit(#_ENTRY_), _PARENT_##_##_ENTRY_)

#define data_reg_const_custom(_REG_, _PARENT_, _NAME_, _VALUE_)                                    \
  data_reg_const((_REG_), t_##_PARENT_, string_lit(#_NAME_), _VALUE_)

void data_reg_const(DataReg*, DataType parent, String name, i32 value);

/**
 * Attach a comment to the given type.
 * Pre-condition: Type is declared in the registry.
 */
#define data_reg_comment_t(_REG_, _DATA_TYPE_, _COMMENT_LIT_)                                      \
  data_reg_comment((_REG_), t_##_DATA_TYPE_, string_lit(_COMMENT_LIT_))

void data_reg_comment(DataReg*, DataType, String comment);
