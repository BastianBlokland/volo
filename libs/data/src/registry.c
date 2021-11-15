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

DataType data_type_prim(const DataPrim prim) {
  const DataType type = (DataType)prim;
  if (LIKELY(data_decl(type)->kind != DataKind_Invalid)) {
    return type;
  }
  switch (prim) {
#define X(_T_)                                                                                     \
  case DataPrim_##_T_:                                                                             \
    *data_decl(type) = (DataDecl){                                                                 \
        .kind  = DataKind_##_T_,                                                                   \
        .size  = sizeof(_T_),                                                                      \
        .align = alignof(_T_),                                                                     \
        .id    = {.name = string_lit(#_T_), .hash = bits_hash_32(string_lit(#_T_))},               \
    };                                                                                             \
    break;
    DATA_PRIMS
#undef X
  case DataPrim_Count:
    diag_crash_msg("Out of bound DataPrim");
  }
  return type;
}

DataType data_type_by_name(const String name) { return data_type_by_hash(bits_hash_32(name)); }

DataType data_type_by_hash(const u32 nameHash) {
  const usize typeCount = (usize)thread_atomic_load_i64(&g_nextIdCounter);
  for (DataType i = 0; i != typeCount; ++i) {
    if (data_decl(i)->id.hash == nameHash) {
      return i;
    }
  }
  return sentinel_u32;
}

DataKind data_type_kind(const DataType type) { return data_decl(type)->kind; }
DataId   data_type_id(const DataType type) { return data_decl(type)->id; }
usize    data_type_size(const DataType type) { return data_decl(type)->size; }
usize    data_type_align(const DataType type) { return data_decl(type)->size; }

usize data_type_fields(const DataType type) {
  if (LIKELY(data_type_kind(type) == DataKind_Struct)) {
    return data_decl(type)->val_struct.count;
  }
  return 0;
}

usize data_type_consts(const DataType type) {
  if (LIKELY(data_type_kind(type) == DataKind_Enum)) {
    return data_decl(type)->val_enum.count;
  }
  return 0;
}

DataType data_register_struct(const String name, const usize size, const usize align) {
  const u32 nameHash = bits_hash_32(name);

  diag_assert_msg(bits_ispow2(align), "Alignment '{}' is not a power-of-two", fmt_int(align));
  diag_assert_msg(
      bits_aligned(size, align),
      "Size '{}' is not a multiple of alignment '{}'",
      fmt_size(size),
      fmt_int(align));
  diag_assert_msg(
      sentinel_check(data_type_by_hash(nameHash)), "Duplicate type with name '{}'", fmt_text(name));

  const DataType type = data_alloc_type();
  *data_decl(type)    = (DataDecl){
      .kind       = DataKind_Struct,
      .size       = size,
      .align      = align,
      .id         = {.name = string_dup(g_alloc_persist, name), .hash = nameHash},
      .val_struct = {.fields = alloc_array_t(g_alloc_persist, DataDeclField, data_max_fields)}};
  return type;
}

void data_register_field(
    const DataType parentId, const String name, const usize offset, const DataMeta meta) {

  const u32 nameHash = bits_hash_32(name);
  DataDecl* parent   = data_decl(parentId);

  diag_assert_msg(parent->kind == DataKind_Struct, "Constant parent has to be a Struct");
  diag_assert_msg(
      parent->val_struct.count < data_max_fields,
      "Struct '{}' has more fields then the maximum of '{}'",
      fmt_text(data_type_id(parentId).name),
      fmt_int(data_max_consts));
  diag_assert_msg(
      offset + data_type_size(meta.type) <= data_type_size(parentId),
      "Offset '{}' is out of bounds for the Struct type",
      fmt_int(offset));

  const usize i                = parent->val_struct.count++;
  parent->val_struct.fields[i] = (DataDeclField){
      .id     = {.name = string_dup(g_alloc_persist, name), .hash = nameHash},
      .offset = offset,
      .meta   = meta,
  };
}

DataType data_register_enum(const String name) {
  const u32 nameHash = bits_hash_32(name);
  diag_assert_msg(
      sentinel_check(data_type_by_hash(nameHash)), "Duplicate type with name '{}'", fmt_text(name));

  const DataType type = data_alloc_type();
  *data_decl(type)    = (DataDecl){
      .kind     = DataKind_Enum,
      .size     = sizeof(i32),
      .align    = alignof(i32),
      .id       = {.name = string_dup(g_alloc_persist, name), .hash = nameHash},
      .val_enum = {.consts = alloc_array_t(g_alloc_persist, DataDeclConst, data_max_consts)},
  };
  return type;
}

void data_register_const(const DataType parentId, const String name, const i32 value) {
  const u32 nameHash = bits_hash_32(name);
  DataDecl* parent   = data_decl(parentId);

  diag_assert_msg(parent->kind == DataKind_Enum, "Constant parent has to be an Enum");
  diag_assert_msg(
      parent->val_enum.count < data_max_consts,
      "Enum '{}' has more constants then the maximum of '{}'",
      fmt_text(data_type_id(parentId).name),
      fmt_int(data_max_consts));

  const usize i              = parent->val_enum.count++;
  parent->val_enum.consts[i] = (DataDeclConst){
      .id    = {.name = string_dup(g_alloc_persist, name), .hash = nameHash},
      .value = value,
  };
}

DataDecl* data_decl(const DataType type) { return &g_types[type]; }
