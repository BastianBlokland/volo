#include "check_spec.h"
#include "core_alloc.h"
#include "script_binder.h"
#include "script_error.h"
#include "script_val.h"

typedef struct {
  u32 counterA, counterB;
} ScriptBindTestCtx;

static ScriptVal test_bind_a(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)args;
  (void)err;

  ScriptBindTestCtx* typedCtx = ctx;
  ++typedCtx->counterA;
  return script_null();
}

static ScriptVal test_bind_b(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)args;
  (void)err;

  ScriptBindTestCtx* typedCtx = ctx;
  ++typedCtx->counterB;
  return script_null();
}

spec(binder) {

  ScriptBinder* binder = null;

  setup() { binder = script_binder_create(g_alloc_heap); }

  it("sorts bindings on the string-hash") {
    const ScriptSig* nullSig = null;
    script_binder_declare(binder, string_lit("a"), nullSig, null);
    script_binder_declare(binder, string_lit("b"), nullSig, null);
    script_binder_declare(binder, string_lit("c"), nullSig, null);
    script_binder_declare(binder, string_lit("d"), nullSig, null);
    script_binder_declare(binder, string_lit("e"), nullSig, null);
    script_binder_finalize(binder);

    check_eq_int(script_binder_lookup(binder, string_hash_lit("b")), 0);
    check_eq_int(script_binder_lookup(binder, string_hash_lit("c")), 1);
    check_eq_int(script_binder_lookup(binder, string_hash_lit("d")), 2);
    check_eq_int(script_binder_lookup(binder, string_hash_lit("e")), 3);
    check_eq_int(script_binder_lookup(binder, string_hash_lit("a")), 4);
  }

  it("can execute bound functions") {
    ScriptError err = {0};

    const String a = string_lit("a");
    const String b = string_lit("b");

    const ScriptSig* nullSig = null;
    script_binder_declare(binder, a, nullSig, test_bind_a);
    script_binder_declare(binder, b, nullSig, test_bind_b);
    script_binder_finalize(binder);

    ScriptBindTestCtx ctx  = {0};
    const ScriptArgs  args = {0};

    script_binder_exec(binder, script_binder_lookup(binder, string_hash(a)), &ctx, args, &err);
    check_eq_int(ctx.counterA, 1);
    check_eq_int(ctx.counterB, 0);

    script_binder_exec(binder, script_binder_lookup(binder, string_hash(b)), &ctx, args, &err);
    check_eq_int(ctx.counterA, 1);
    check_eq_int(ctx.counterB, 1);
  }

  teardown() { script_binder_destroy(binder); }
}
