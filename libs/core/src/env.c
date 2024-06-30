#include "core_alloc.h"
#include "core_env.h"

#define env_var_max_value_size (usize_kibibyte * 32)

String env_var_scratch(const String name) {
  Mem      scratchMem    = alloc_alloc(g_allocScratch, env_var_max_value_size, 1);
  DynArray scratchWriter = dynstring_create_over(scratchMem);

  if (env_var(name, &scratchWriter)) {
    return dynstring_view(&scratchWriter);
  }
  return string_empty;
}
