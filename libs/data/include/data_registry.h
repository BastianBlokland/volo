#pragma once
#include "data_type.h"

/**
 * Data registry, container for data-type definitions.
 */
typedef struct sDataReg DataReg;

typedef enum {
  DataContainer_None,        // eg. 'f32 value;'.
  DataContainer_Pointer,     // eg. 'f32* value;'.
  DataContainer_InlineArray, // eg. 'f32 values[123];', NOTE: The count is stored in 'fixedCount'.
  DataContainer_HeapArray,   // eg. 'HeapArray_t(f32) values;'.
  DataContainer_DynArray,    // eg. 'DynArray values;'.
} DataContainer;

typedef enum {
  DataFlags_None           = 0,
  DataFlags_Opt            = 1 << 0,
  DataFlags_NotEmpty       = 1 << 1,
  DataFlags_Intern         = 1 << 2, // Intern the string in the global string-table.
  DataFlags_ExternalMemory = 1 << 3, // Support external allocations on this memory type.
  DataFlags_TransferToBase = DataFlags_Intern | DataFlags_ExternalMemory,
} DataFlags;

/**
 * Meta information for a data value.
 * Combination of a type and properties of a specific instance (for example if its a pointer).
 */
typedef union {
  struct {
    DataType      type;
    DataFlags     flags : 8;
    DataContainer container : 8;
    u32           fixedCount : 16; // Size of fixed size containers (for example inline-array).
  };
  u64 data;
} DataMeta;

ASSERT(sizeof(DataMeta) == 8, "Unexpected DataMeta size");

#define data_meta_t(_DATA_TYPE_, ...) ((DataMeta){.type = _DATA_TYPE_, ##__VA_ARGS__})
#define data_prim_t(_PRIM_) ((DataType)DataKind_##_PRIM_)

/**
 * Global data registry.
 */
extern DataReg* g_dataReg;

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
 * Retrieve the total number of registered types.
 */
u32 data_type_count(const DataReg*);

/**
 * Lookup a type by name.
 * NOTE: Returns 0 if no type was found with a matching name.
 */
DataType data_type_from_name(const DataReg*, String name);
DataType data_type_from_name_hash(const DataReg*, StringHash nameHash);

/**
 * Retrieve the name of a registered type.
 */
String     data_name(const DataReg*, DataType);
StringHash data_name_hash(const DataReg*, DataType);
String     data_const_name(const DataReg*, DataType enumType, i32 value);

/**
 * Retrieve the size (in bytes) of a registered type.
 */
usize data_size(const DataReg*, DataType);
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
usize data_meta_align(const DataReg*, DataMeta);

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
      sizeof(((_PARENT_*)0)->_FIELD_),                                                             \
      offsetof(_PARENT_, _FIELD_),                                                                 \
      data_meta_t(_DATA_TYPE_, __VA_ARGS__))

void data_reg_field(DataReg*, DataType parent, String name, usize size, usize offset, DataMeta);

/**
 * Register a new Union type.
 */
#define data_reg_union_t(_REG_, _T_, _TAG_FIELD_)                                                  \
  MAYBE_UNUSED const DataType t_##_T_ = data_reg_union(                                            \
      (_REG_), string_lit(#_T_), sizeof(_T_), alignof(_T_), offsetof(_T_, _TAG_FIELD_))

DataType data_reg_union(DataReg*, String name, usize size, usize align, usize tagOffset);

/**
 * Register a name field for the given union type.
 */
#define data_reg_union_name_t(_REG_, _PARENT_, _NAME_FIELD_)                                       \
  data_reg_union_name((_REG_), t_##_PARENT_, offsetof(_PARENT_, _NAME_FIELD_))

void data_reg_union_name(DataReg*, DataType, usize nameOffset);

/**
 * Register a new choice for a Union,
 */
#define data_reg_choice_t(_REG_, _PARENT_, _TAG_, _FIELD_, _DATA_TYPE_, ...)                       \
  data_reg_choice(                                                                                 \
      (_REG_),                                                                                     \
      t_##_PARENT_,                                                                                \
      string_lit(#_TAG_),                                                                          \
      (_TAG_),                                                                                     \
      sizeof(((_PARENT_*)0)->_FIELD_),                                                             \
      offsetof(_PARENT_, _FIELD_),                                                                 \
      data_meta_t(_DATA_TYPE_, __VA_ARGS__))

#define data_reg_choice_empty(_REG_, _PARENT_, _TAG_)                                              \
  data_reg_choice((_REG_), t_##_PARENT_, string_lit(#_TAG_), (_TAG_), 0, 0, (DataMeta){0})

void data_reg_choice(
    DataReg*, DataType parent, String name, i32 tag, usize size, usize offset, DataMeta);

/**
 * Register a new Enum type (optionally supporting multiple values, aka flags).
 */
#define data_reg_enum_t(_REG_, _T_)                                                                \
  MAYBE_UNUSED const DataType t_##_T_ = data_reg_enum((_REG_), string_lit(#_T_), false)

#define data_reg_enum_multi_t(_REG_, _T_)                                                          \
  MAYBE_UNUSED const DataType t_##_T_ = data_reg_enum((_REG_), string_lit(#_T_), true)

DataType data_reg_enum(DataReg*, String name, bool multi);

/**
 * Register a new constant for an Enum.
 */
#define data_reg_const_t(_REG_, _PARENT_, _ENTRY_)                                                 \
  data_reg_const((_REG_), t_##_PARENT_, string_lit(#_ENTRY_), _PARENT_##_##_ENTRY_)

#define data_reg_const_custom(_REG_, _PARENT_, _NAME_, _VALUE_)                                    \
  data_reg_const((_REG_), t_##_PARENT_, string_lit(#_NAME_), _VALUE_)

void data_reg_const(DataReg*, DataType parent, String name, i32 value);

/**
 * Register a new Opaque type.
 */
#define data_reg_opaque_t(_REG_, _T_)                                                              \
  MAYBE_UNUSED const DataType t_##_T_ =                                                            \
      data_reg_opaque((_REG_), string_lit(#_T_), sizeof(_T_), alignof(_T_))

DataType data_reg_opaque(DataReg*, String name, usize size, usize align);

/**
 * Attach a comment to the given type.
 * Pre-condition: Type is declared in the registry.
 */
#define data_reg_comment_t(_REG_, _DATA_TYPE_, _COMMENT_LIT_)                                      \
  data_reg_comment((_REG_), t_##_DATA_TYPE_, string_lit(_COMMENT_LIT_))

void data_reg_comment(DataReg*, DataType, String comment);
