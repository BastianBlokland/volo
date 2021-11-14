#pragma once
#include "core_dynarray.h"
#include "data_registry.h"

#define data_struct_max_fields 64
#define data_enum_max_entries 64

typedef enum {
  DataKind_Primitive,
  DataKind_Struct,
  DataKind_Enum,
} DataKind;

typedef struct {
  String    name;
  DataType* type;
} DataStructField;

typedef struct {
  DataStructField* fields;
  usize            fieldCount;
} DataTypeStruct;

typedef struct {
  String name;
  i32    value;
} DataEnumEntry;

typedef struct {
  DataEnumEntry* entries;
  usize          entryCount;
} DataTypeEnum;

struct sDataType {
  DataKind kind : 8;
  u16      size, align;
  String   name;
  union {
    DataTypeStruct val_struct;
    DataTypeEnum   val_enum;
  };
};
