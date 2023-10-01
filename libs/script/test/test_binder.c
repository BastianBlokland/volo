#include "check_spec.h"
#include "core_alloc.h"
#include "script_binder.h"
#include "script_val.h"

typedef struct {
  u32 counterA, counterB;
} ScriptBindTestCtx;

static ScriptVal test_bind_a(void* ctx, const ScriptArgs args) {
  (void)args;

  ScriptBindTestCtx* typedCtx = ctx;
  ++typedCtx->counterA;
  return script_null();
}

static ScriptVal test_bind_b(void* ctx, const ScriptArgs args) {
  (void)args;

  ScriptBindTestCtx* typedCtx = ctx;
  ++typedCtx->counterB;
  return script_null();
}

spec(binder) {

  ScriptBinder* binder = null;

  setup() { binder = script_binder_create(g_alloc_heap); }

  it("sorts bindings on the string-hash") {
    script_binder_declare(binder, 10, null);
    script_binder_declare(binder, 5, null);
    script_binder_declare(binder, 42, null);
    script_binder_declare(binder, 1, null);
    script_binder_declare(binder, 7, null);
    script_binder_finalize(binder);

    check_eq_int(script_binder_lookup(binder, 1), 0);
    check_eq_int(script_binder_lookup(binder, 5), 1);
    check_eq_int(script_binder_lookup(binder, 7), 2);
    check_eq_int(script_binder_lookup(binder, 10), 3);
    check_eq_int(script_binder_lookup(binder, 42), 4);
  }

  it("can execute bound functions") {
    script_binder_declare(binder, string_hash_lit("a"), test_bind_a);
    script_binder_declare(binder, string_hash_lit("b"), test_bind_b);
    script_binder_finalize(binder);

    ScriptBindTestCtx ctx  = {0};
    const ScriptArgs  args = {0};

    script_binder_exec(binder, script_binder_lookup(binder, string_hash_lit("a")), &ctx, args);
    check_eq_int(ctx.counterA, 1);
    check_eq_int(ctx.counterB, 0);

    script_binder_exec(binder, script_binder_lookup(binder, string_hash_lit("b")), &ctx, args);
    check_eq_int(ctx.counterA, 1);
    check_eq_int(ctx.counterB, 1);
  }

  teardown() { script_binder_destroy(binder); }
}
