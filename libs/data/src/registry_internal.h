#pragma once
#include "data_registry.h"

typedef struct {
  String name;
  u32    hash;
} DataId;

typedef struct {
  DataId   id;
  usize    offset;
  DataMeta meta;
} DataDeclField;

typedef struct {
  DataDeclField* fields;
  usize          count;
} DataDeclStruct;

typedef struct {
  DataId id;
  i32    value;
} DataDeclConst;

typedef struct {
  DataDeclConst* consts;
  usize          count;
} DataDeclEnum;

typedef struct {
  DataKind kind;
  usize    size, align;
  DataId   id;
  union {
    DataDeclStruct val_struct;
    DataDeclEnum   val_enum;
  };
} DataDecl;

DataDecl* data_decl(DataType);
