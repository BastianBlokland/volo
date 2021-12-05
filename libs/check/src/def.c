#include "core_alloc.h"

#include "def_internal.h"

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

void check_register_spec(CheckDef* ctx, String name, CheckSpecRoutine routine) {
  *dynarray_push_t(&ctx->specs, CheckSpecDef) = (CheckSpecDef){
      .name    = string_dup(ctx->alloc, name),
      .routine = routine,
  };
}
