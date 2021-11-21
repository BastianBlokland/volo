#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "data_utils.h"

#include "registry_internal.h"

static void data_destroy_string(Allocator* alloc, const Mem data) {
  const String val = *mem_as_t(data, String);
  if (!string_is_empty(val)) {
    string_free(alloc, val);
  }
}

static void data_destroy_struct(Allocator* alloc, const DataMeta meta, const Mem data) {
  data_for_fields(meta.type, field, {
    const Mem fieldMem = data_field_mem(field, data);
    data_destroy(alloc, field->meta, fieldMem);
  });
}

static void data_destroy_single(Allocator* alloc, const DataMeta meta, const Mem data) {
  switch (data_decl(meta.type)->kind) {
  case DataKind_bool:
  case DataKind_i8:
  case DataKind_i16:
  case DataKind_i32:
  case DataKind_i64:
  case DataKind_u8:
  case DataKind_u16:
  case DataKind_u32:
  case DataKind_u64:
  case DataKind_f32:
  case DataKind_f64:
  case DataKind_Enum:
    return;
  case DataKind_String:
    data_destroy_string(alloc, data);
    return;
  case DataKind_Struct:
    data_destroy_struct(alloc, meta, data);
    return;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static void data_destroy_pointer(Allocator* alloc, const DataMeta meta, const Mem data) {
  void* ptr = *mem_as_t(data, void*);
  if (!ptr) {
    return;
  }
  const Mem targetMem = mem_create(ptr, data_size(meta.type));
  data_destroy_single(alloc, data_meta_base(meta), targetMem);

  alloc_free(alloc, targetMem);
}

static void data_destroy_array(Allocator* alloc, const DataMeta meta, const Mem data) {
  const DataDecl*  decl  = data_decl(meta.type);
  const DataArray* array = mem_as_t(data, DataArray);
  if (!array->count) {
    return;
  }

  for (usize i = 0; i != array->count; ++i) {
    data_destroy_single(alloc, data_meta_base(meta), data_elem_mem(decl, array, i));
  }

  alloc_free(alloc, mem_create(array->data, decl->size * array->count));
}

void data_destroy(Allocator* alloc, const DataMeta meta, const Mem data) {
  switch (meta.container) {
  case DataContainer_None:
    data_destroy_single(alloc, meta, data);
    return;
  case DataContainer_Pointer:
    data_destroy_pointer(alloc, meta, data);
    return;
  case DataContainer_Array:
    data_destroy_array(alloc, meta, data);
    return;
  }
  diag_crash();
}
