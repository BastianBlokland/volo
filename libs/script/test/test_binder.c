#include "check/spec.h"
#include "core/alloc.h"
#include "core/dynstring.h"
#include "script/binder.h"
#include "script/val.h"

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

  setup() {
    binder = script_binder_create(g_allocHeap, string_lit("test"), ScriptBinderFlags_DevSupport);
  }

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

  it("stores declared memory keys") {
    script_binder_mem_key_push(binder, string_lit("Foo"), string_lit("Doc for Foo."));
    script_binder_mem_key_push(binder, string_lit("Bar"), string_lit("Doc for Bar."));
    script_binder_finalize(binder);

    check_eq_int(script_binder_mem_key_count(binder), 2);

    check_eq_int(script_binder_mem_key_name(binder, 0), string_hash_lit("Foo"));
    check_eq_string(script_binder_mem_key_doc(binder, 0), string_lit("Doc for Foo."));

    check_eq_int(script_binder_mem_key_name(binder, 1), string_hash_lit("Bar"));
    check_eq_string(script_binder_mem_key_doc(binder, 1), string_lit("Doc for Bar."));
  }

  it("ignores memory key declarations without DevSupport") {
    script_binder_destroy(binder);
    binder = script_binder_create(g_allocHeap, string_lit("test"), ScriptBinderFlags_None);
    script_binder_mem_key_push(binder, string_lit("Foo"), string_lit("Doc for Foo."));
    script_binder_finalize(binder);

    check_eq_int(script_binder_mem_key_count(binder), 0);
  }

  it("round-trips memory keys through serialization") {
    script_binder_mem_key_push(binder, string_lit("Alpha"), string_lit("Doc for Alpha."));
    script_binder_mem_key_push(binder, string_lit("Beta"), string_empty);
    script_binder_finalize(binder);

    DynString str = dynstring_create(g_allocHeap, 512);
    script_binder_write(&str, binder);

    ScriptBinder* copy = script_binder_read(g_allocHeap, dynstring_view(&str));
    check_require(copy != null);

    check_eq_int(script_binder_mem_key_count(copy), 2);
    check_eq_int(script_binder_mem_key_name(copy, 0), string_hash_lit("Alpha"));
    check_eq_string(script_binder_mem_key_doc(copy, 0), string_lit("Doc for Alpha."));
    check_eq_int(script_binder_mem_key_name(copy, 1), string_hash_lit("Beta"));
    check_eq_string(script_binder_mem_key_doc(copy, 1), string_empty);

    script_binder_destroy(copy);
    dynstring_destroy(&str);
  }

  teardown() { script_binder_destroy(binder); }
}
