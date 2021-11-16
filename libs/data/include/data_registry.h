#pragma once
#include "data_type.h"

typedef enum {
  DataContainer_None,
  DataContainer_Pointer,
  DataContainer_Array,
} DataContainer;

typedef struct {
  DataType      type;
  DataContainer container;
} DataMeta;

DataType data_type_prim(DataPrim);

DataType data_register_struct(String name, usize size, usize align);
void     data_register_field(DataType parentId, String name, usize offset, DataMeta);
DataType data_register_enum(String name);
void     data_register_const(DataType parentId, String name, i32 value);
