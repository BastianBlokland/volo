#include "check_spec.h"
#include "core_alloc.h"
#include "script_binder.h"
#include "script_val.h"

typedef struct {
  u32 counterA, counterB;
} ScriptBindTestCtx;

static ScriptVal test_bind_a(void* ctx, ScriptBinderCall* call) {
  (void)call;

  ScriptBindTestCtx* typedCtx = ctx;
  ++typedCtx->counterA;
  return script_null();
}

static ScriptVal test_bind_b(void* ctx, ScriptBinderCall* call) {
  (void)call;

  ScriptBindTestCtx* typedCtx = ctx;
  ++typedCtx->counterB;
  return script_null();
}

spec(binder) {

  ScriptBinder* binder = null;

  setup() { binder = script_binder_create(g_allocHeap); }

  it("sorts bindings on the string-hash") {
    const String     doc     = string_empty;
    const ScriptSig* nullSig = null;
    script_binder_declare(binder, string_lit("a"), doc, nullSig, null);
    script_binder_declare(binder, string_lit("b"), doc, nullSig, null);
    script_binder_declare(binder, string_lit("c"), doc, nullSig, null);
    script_binder_declare(binder, string_lit("d"), doc, nullSig, null);
    script_binder_declare(binder, string_lit("e"), doc, nullSig, null);
    script_binder_finalize(binder);

    check_eq_int(script_binder_slot_lookup(binder, string_hash_lit("b")), 0);
    check_eq_int(script_binder_slot_lookup(binder, string_hash_lit("c")), 1);
    check_eq_int(script_binder_slot_lookup(binder, string_hash_lit("d")), 2);
    check_eq_int(script_binder_slot_lookup(binder, string_hash_lit("e")), 3);
    check_eq_int(script_binder_slot_lookup(binder, string_hash_lit("a")), 4);
  }

  it("can execute bound functions") {
    const String a = string_lit("a");
    const String b = string_lit("b");

    const String     doc     = string_empty;
    const ScriptSig* nullSig = null;
    script_binder_declare(binder, a, doc, nullSig, test_bind_a);
    script_binder_declare(binder, b, doc, nullSig, test_bind_b);
    script_binder_finalize(binder);

    ScriptBindTestCtx ctx  = {0};
    ScriptBinderCall  call = {0};

    script_binder_exec(binder, script_binder_slot_lookup(binder, string_hash(a)), &ctx, &call);
    check_eq_int(ctx.counterA, 1);
    check_eq_int(ctx.counterB, 0);

    script_binder_exec(binder, script_binder_slot_lookup(binder, string_hash(b)), &ctx, &call);
    check_eq_int(ctx.counterA, 1);
    check_eq_int(ctx.counterB, 1);
  }

  teardown() { script_binder_destroy(binder); }
}
