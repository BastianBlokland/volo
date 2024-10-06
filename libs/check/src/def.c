#include "core_alloc.h"
#include "core_diag.h"

#include "def_internal.h"

/**
 * Trim any leading and or trailing underscores.
 * Reason is so '_' can be used when the spec name is not a valid c identifier (for example 'enum').
 */
static String check_spec_name_normalize(const String name) {
  return string_trim(name, string_lit("_"));
}

CheckDef* check_create(Allocator* alloc) {
  CheckDef* ctx = alloc_alloc_t(alloc, CheckDef);
  *ctx          = (CheckDef){
      .specs = dynarray_create_t(alloc, CheckSpecDef, 64),
      .alloc = alloc,
  };
  return ctx;
}

void check_destroy(CheckDef* ctx) {
  dynarray_for_t(&ctx->specs, CheckSpecDef, spec) { string_free(ctx->alloc, spec->name); }

  dynarray_destroy(&ctx->specs);

  alloc_free_t(ctx->alloc, ctx);
}

void check_register_spec(CheckDef* ctx, const String name, CheckSpecRoutine routine) {
  const String nameNorm = check_spec_name_normalize(name);
  diag_assert_msg(!string_is_empty(nameNorm), "Spec name cannot be empty");

  *dynarray_push_t(&ctx->specs, CheckSpecDef) = (CheckSpecDef){
      .name    = string_dup(ctx->alloc, nameNorm),
      .routine = routine,
  };
}
