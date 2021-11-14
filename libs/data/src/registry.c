#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"

#include "registry_internal.h"

#define data_type_max 256
#define data_type_id_start 1
#define data_type_index(_ID_) ((_ID_)-data_type_id_start)

static bool           g_init;
static ThreadSpinLock g_registryLock;
static DataTypeDecl   g_types[data_type_max];

#define X(_NAME_) data_type_define(_NAME_);
DATA_PRIMS
#undef X

static void data_registry_lock() { thread_spinlock_lock(&g_registryLock); }
static void data_registry_unlock() { thread_spinlock_unlock(&g_registryLock); }

static DataType data_register_type(const DataTypeDecl decl) {
  static i64     g_idCounter = data_type_id_start;
  const DataType id          = (DataType)thread_atomic_add_i64(&g_idCounter, 1);
  diag_assert_msg(
      id < data_type_max, "More then '{}' data types are not supported", fmt_int(data_type_max));

  diag_assert_msg(
      bits_ispow2(decl.align), "Type alignment '{}' is not a power-of-two", fmt_int(decl.align));

  diag_assert_msg(
      bits_aligned(decl.size, decl.align),
      "Data size '{}' is not a multiple of the alignment '{}'",
      fmt_size(decl.size),
      fmt_int(decl.align));

  g_types[id] = decl;
  return id;
}

static void data_register_prims() {
#define X(_T_)                                                                                     \
  *data_type_ptr(_T_) = data_register_type((DataTypeDecl){                                         \
      .kind  = DataKind_Primitive,                                                                 \
      .name  = string_lit(#_T_),                                                                   \
      .size  = sizeof(_T_),                                                                        \
      .align = alignof(_T_),                                                                       \
  });
  DATA_PRIMS
#undef X
}

static void data_registry_init() {
  if (LIKELY(g_init)) {
    return;
  }
  data_registry_lock();
  if (!g_init) {
    data_register_prims();
    g_init = true;
  }
  data_registry_unlock();
}

static DataTypeDecl* data_type_decl(const DataType type) {
  data_registry_init();
  diag_assert_msg(type, "Data-type has not been initialized");
  return &g_types[data_type_index(type)];
}

DataKind data_type_kind(const DataType type) { return data_type_decl(type)->kind; }
String   data_type_name(const DataType type) { return data_type_decl(type)->name; }
usize    data_type_size(const DataType type) { return data_type_decl(type)->size; }
usize    data_type_align(const DataType type) { return data_type_decl(type)->align; }

DataType data_type_register_struct(
    DataType*                    var,
    const DataTypeConfig*        cfg,
    const DataStructFieldConfig* fieldConfigs,
    const usize                  fieldCount) {

  diag_assert(var);
  if (*var) {
    return *var; // Type has already been registered.
  }
  data_registry_init();

  DataStructField* fields = alloc_array_t(g_alloc_persist, DataStructField, fieldCount);
  for (usize i = 0; i != fieldCount; ++i) {
    fields[i] = (DataStructField){
        .name   = string_dup(g_alloc_persist, fieldConfigs[i].name),
        .offset = fieldConfigs[i].offset,
        .type   = fieldConfigs[i].type,
    };
  }
  DataTypeDecl decl = {
      .kind       = DataKind_Struct,
      .name       = string_dup(g_alloc_persist, cfg->name),
      .size       = cfg->size,
      .align      = cfg->align,
      .val_struct = {.fields = fields, .fieldCount = fieldCount},
  };
  return *var = data_register_type(decl);
}

DataType data_type_register_enum(
    DataType*                  var,
    const DataTypeConfig*      cfg,
    const DataEnumEntryConfig* entryConfigs,
    const usize                entryCount) {

  diag_assert(var);
  if (*var) {
    return *var; // Type has already been registered.
  }
  data_registry_init();

  DataEnumEntry* entries = alloc_array_t(g_alloc_persist, DataEnumEntry, entryCount);
  for (usize i = 0; i != entryCount; ++i) {
    entries[i] = (DataEnumEntry){
        .name  = string_dup(g_alloc_persist, entryConfigs[i].name),
        .value = entryConfigs[i].value,
    };
  }
  DataTypeDecl decl = {
      .kind     = DataKind_Enum,
      .name     = string_dup(g_alloc_persist, cfg->name),
      .size     = cfg->size,
      .align    = cfg->align,
      .val_enum = {.entries = entries, .entryCount = entryCount},
  };
  return *var = data_register_type(decl);
}
