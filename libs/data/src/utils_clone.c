#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "data_utils.h"

#include "registry_internal.h"

static void data_clone_string(Allocator* alloc, const Mem original, const Mem clone) {
  const String originalVal = *mem_as_t(original, String);
  if (string_is_empty(originalVal)) {
    *mem_as_t(clone, String) = string_empty;
  } else {
    *mem_as_t(clone, String) = string_dup(alloc, originalVal);
  }
}

static void
data_clone_struct(Allocator* alloc, const DataMeta meta, const Mem original, const Mem clone) {
  data_for_fields(meta.type, field, {
    const Mem originalFieldMem = data_field_mem(field, original);
    const Mem dataFieldMem     = data_field_mem(field, clone);
    data_clone(alloc, field->meta, originalFieldMem, dataFieldMem);
  });
}

static void
data_clone_single(Allocator* alloc, const DataMeta meta, const Mem original, const Mem clone) {
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
    mem_cpy(clone, original);
    return;
  case DataKind_String:
    data_clone_string(alloc, original, clone);
    return;
  case DataKind_Struct:
    data_clone_struct(alloc, meta, original, clone);
    return;
  case DataKind_Invalid:
  case DataKind_Count:
    break;
  }
  diag_crash();
}

static void
data_clone_pointer(Allocator* alloc, const DataMeta meta, const Mem original, const Mem clone) {
  void* originalPtr = *mem_as_t(original, void*);
  if (!originalPtr) {
    *mem_as_t(clone, void*) = null;
    return;
  }

  const DataDecl* decl        = data_decl(meta.type);
  const Mem       originalMem = mem_create(originalPtr, decl->size);
  const Mem       newMem      = alloc_alloc(alloc, decl->size, decl->align);
  *mem_as_t(clone, void*)     = newMem.ptr;

  data_clone_single(alloc, data_meta_base(meta), originalMem, newMem);
}

static void
data_clone_array(Allocator* alloc, const DataMeta meta, const Mem original, const Mem clone) {
  const DataDecl*  decl          = data_decl(meta.type);
  const DataArray* originalArray = mem_as_t(original, DataArray);
  const usize      count         = originalArray->count;
  if (!count) {
    *mem_as_t(clone, DataArray) = (DataArray){0};
    return;
  }

  const Mem  newArrayMem = alloc_alloc(alloc, decl->size * count, decl->align);
  DataArray* newArray    = mem_as_t(clone, DataArray);
  *newArray              = (DataArray){.data = newArrayMem.ptr, .count = count};

  for (usize i = 0; i != count; ++i) {
    const Mem originalElemMem = data_elem_mem(decl, originalArray, i);
    const Mem newElemMem      = data_elem_mem(decl, newArray, i);
    data_clone_single(alloc, data_meta_base(meta), originalElemMem, newElemMem);
  }
}

void data_clone(Allocator* alloc, const DataMeta meta, const Mem original, const Mem clone) {
  switch (meta.container) {
  case DataContainer_None:
    data_clone_single(alloc, meta, original, clone);
    return;
  case DataContainer_Pointer:
    data_clone_pointer(alloc, meta, original, clone);
    return;
  case DataContainer_Array:
    data_clone_array(alloc, meta, original, clone);
    return;
  }
  diag_crash();
}
