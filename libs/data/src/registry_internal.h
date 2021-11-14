#pragma once
#include "data_registry.h"

typedef struct {
  String    name;
  usize     offset;
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

typedef struct {
  DataKind kind;
  usize    size, align;
  String   name;
  union {
    DataTypeStruct val_struct;
    DataTypeEnum   val_enum;
  };
} DataTypeDecl;
