#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data_registry.h"

#include "registry_internal.h"

#define data_max_types 256
#define data_max_fields 32
#define data_max_consts 64

static DataDecl g_types[data_max_types];
static i64      g_nextIdCounter = DataPrim_Count;

static DataType data_alloc_type() {
  const DataType type = (DataType)thread_atomic_add_i64(&g_nextIdCounter, 1);
  diag_assert_msg(
      type < data_max_types, "More then '{}' types are not supported", fmt_int(data_max_types));
  return type;
}

static DataId data_get_id(const String name) {
  return (DataId){.name = name, .hash = bits_hash_32(name)};
}

static DataType data_type_by_id(const DataId id) {
  const usize typeCount = (usize)thread_atomic_load_i64(&g_nextIdCounter);
  for (DataType i = 1; i != typeCount; ++i) {
    if (data_decl(i)->id.hash == id.hash) {
      return i;
    }
  }
  return sentinel_u32;
}

DataType data_prim(const DataPrim prim) {
  static bool           g_initialized;
  static ThreadSpinLock g_initLock;

  const DataType type = (DataType)prim;
  if (LIKELY(g_initialized)) {
    return type;
  }
  thread_spinlock_lock(&g_initLock);
  if (UNLIKELY(g_initialized)) {
    goto done;
  }
#define X(_T_)                                                                                     \
  *data_decl((DataType)DataPrim_##_T_) = (DataDecl){                                               \
      .kind  = DataKind_##_T_,                                                                     \
      .size  = sizeof(_T_),                                                                        \
      .align = alignof(_T_),                                                                       \
      .id    = data_get_id(string_lit(#_T_)),                                                      \
  };
  DATA_PRIMS
#undef X
done:
  thread_spinlock_unlock(&g_initLock);
  g_initialized = true;
  return type;
}

String data_name(const DataType type) { return data_decl(type)->id.name; }

usize data_size(const DataType type) { return data_decl(type)->size; }

usize data_align(const DataType type) { return data_decl(type)->align; }

usize data_meta_size(const DataMeta meta) {
  switch (meta.container) {
  case DataContainer_None:
    return data_decl(meta.type)->size;
  case DataContainer_Pointer:
    return sizeof(void*);
  case DataContainer_Array:
    return sizeof(DataArray);
  }
  diag_crash();
}

DataType data_register_struct(const String name, const usize size, const usize align) {
  const DataId id = data_get_id(string_dup(g_alloc_persist, name));

  diag_assert_msg(bits_ispow2(align), "Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      bits_aligned(size, align),
      "Size '{}' is not a multiple of alignment '{}'",
      fmt_size(size),
      fmt_int(align));
  diag_assert_msg(
      sentinel_check(data_type_by_id(id)), "Duplicate type with name '{}'", fmt_text(name));

  const DataType type = data_alloc_type();
  *data_decl(type)    = (DataDecl){
      .kind       = DataKind_Struct,
      .size       = size,
      .align      = align,
      .id         = id,
      .val_struct = {.fields = alloc_array_t(g_alloc_persist, DataDeclField, data_max_fields)},
  };
  return type;
}

void data_register_field(
    const DataType parentId, const String name, const usize offset, const DataMeta meta) {

  const DataId id     = data_get_id(string_dup(g_alloc_persist, name));
  DataDecl*    parent = data_decl(parentId);

  diag_assert_msg(parent->kind == DataKind_Struct, "Constant parent has to be a Struct");
  diag_assert_msg(
      parent->val_struct.count < data_max_fields,
      "Struct '{}' has more fields then the maximum of '{}'",
      fmt_text(data_decl(parentId)->id.name),
      fmt_int(data_max_consts));
  diag_assert_msg(
      offset + data_meta_size(meta) <= data_decl(parentId)->size,
      "Offset '{}' is out of bounds for the Struct type",
      fmt_int(offset));

  const usize i                = parent->val_struct.count++;
  parent->val_struct.fields[i] = (DataDeclField){
      .id     = id,
      .offset = offset,
      .meta   = meta,
  };
}

DataType data_register_enum(const String name) {
  const DataId id = data_get_id(string_dup(g_alloc_persist, name));
  diag_assert_msg(
      sentinel_check(data_type_by_id(id)), "Duplicate type with name '{}'", fmt_text(name));

  const DataType type = data_alloc_type();
  *data_decl(type)    = (DataDecl){
      .kind     = DataKind_Enum,
      .size     = sizeof(i32),
      .align    = alignof(i32),
      .id       = id,
      .val_enum = {.consts = alloc_array_t(g_alloc_persist, DataDeclConst, data_max_consts)},
  };
  return type;
}

void data_register_const(const DataType parentId, const String name, const i32 value) {
  const DataId id     = data_get_id(string_dup(g_alloc_persist, name));
  DataDecl*    parent = data_decl(parentId);

  diag_assert_msg(parent->kind == DataKind_Enum, "Constant parent has to be an Enum");
  diag_assert_msg(
      parent->val_enum.count < data_max_consts,
      "Enum '{}' has more constants then the maximum of '{}'",
      fmt_text(data_decl(parentId)->id.name),
      fmt_int(data_max_consts));

  const usize i              = parent->val_enum.count++;
  parent->val_enum.consts[i] = (DataDeclConst){.id = id, .value = value};
}

DataDecl* data_decl(const DataType type) {
  diag_assert_msg(type, "Uninitialized data-type");
  return &g_types[type - 1];
}

Mem data_field_mem(const DataDeclField* field, Mem structMem) {
  return mem_create(bits_ptr_offset(structMem.ptr, field->offset), data_meta_size(field->meta));
}
