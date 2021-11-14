#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"

#include "registry_internal.h"

#define data_types_max 256

static bool           g_init;
static ThreadSpinLock g_registryLock;
static DataType       g_types[data_types_max];

#define X(_NAME_) data_type_define(_NAME_);
DATA_PRIMS
#undef X

static void data_registry_lock() { thread_spinlock_lock(&g_registryLock); }
static void data_registry_unlock() { thread_spinlock_unlock(&g_registryLock); }

static DataType* data_register_type(const DataType type) {
  static i64 g_idCounter;
  const i64  id = thread_atomic_add_i64(&g_idCounter, 1);
  diag_assert_msg(
      id < data_types_max, "More then '{}' data types are not supported", fmt_int(data_types_max));

  diag_assert_msg(
      bits_ispow2(type.align), "Type alignment '{}' is not a power-of-two", fmt_int(type.align));

  diag_assert_msg(
      bits_aligned(type.size, type.align),
      "Data size '{}' is not a multiple of the alignment '{}'",
      fmt_size(type.size),
      fmt_int(type.align));

  g_types[id] = type;
  return &g_types[id];
}

static void data_register_prims() {
#define X(_T_)                                                                                     \
  *data_type_ptr(_T_) = data_register_type((DataType){                                             \
      .kind  = DataKind_Primitive,                                                                 \
      .name  = string_lit(#_T_),                                                                   \
      .size  = sizeof(_T_),                                                                        \
      .align = alignof(_T_),                                                                       \
  });
  DATA_PRIMS
#undef X
}

static void data_registry_init() {
  if (g_init) {
    return;
  }
  data_registry_lock();
  if (!g_init) {
    data_register_prims();
    g_init = true;
  }
  data_registry_unlock();
}

void data_register_struct(DataTypeStructInit init, DataType** var, const DataTypeConfig* cfg) {
  diag_assert(var);
  if (*var) {
    // Type has already been registered.
    return;
  }

  data_registry_init();

  DataType type = {
      .kind       = DataKind_Struct,
      .name       = string_dup(g_alloc_persist, cfg->name),
      .size       = (u16)cfg->size,
      .align      = (u16)cfg->align,
      .val_struct = {},
  };
  *var = data_register_type(type);
}
