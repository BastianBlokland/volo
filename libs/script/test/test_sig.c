#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_sig.h"
#include "script_val.h"

spec(sig) {
  it("can store ret type") {
    ScriptSig* sig = script_sig_create(g_allocScratch, script_mask_bool, null, 0);

    check_eq_int(script_sig_ret(sig), script_mask_bool);

    script_sig_destroy(sig);
  }

  it("can store ret type and a single argument") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_num},
    };
    ScriptSig* sig = script_sig_create(g_allocScratch, script_mask_bool, args, array_elems(args));

    check_eq_int(script_sig_ret(sig), script_mask_bool);
    check_eq_int(script_sig_arg_count(sig), 1);
    check_eq_string(script_sig_arg(sig, 0).name, args[0].name);
    check_eq_int(script_sig_arg(sig, 0).mask, args[0].mask);

    script_sig_destroy(sig);
  }

  it("can store ret type and multiple arguments") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_num},
        {.name = string_lit("argB"), .mask = script_mask_null},
        {.name = string_lit("argC"), .mask = script_mask_null | script_mask_vec3},
    };
    ScriptSig* sig = script_sig_create(g_allocScratch, script_mask_bool, args, array_elems(args));

    check_eq_int(script_sig_ret(sig), script_mask_bool);
    check_eq_int(script_sig_arg_count(sig), array_elems(args));
    for (u8 i = 0; i != array_elems(args); ++i) {
      check_eq_string(script_sig_arg(sig, i).name, args[i].name);
      check_eq_int(script_sig_arg(sig, i).mask, args[i].mask);
    }

    script_sig_destroy(sig);
  }

  it("can clone signatures with zero arguments") {
    ScriptSig* sigA = script_sig_create(g_allocScratch, script_mask_bool, null, 0);
    ScriptSig* sigB = script_sig_clone(g_allocHeap, sigA);

    check_eq_int(script_sig_ret(sigA), script_sig_ret(sigB));
    check_eq_int(script_sig_arg_count(sigA), script_sig_arg_count(sigB));

    script_sig_destroy(sigA);
    script_sig_destroy(sigB);
  }

  it("can clone signatures with a single argument") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_num},
    };
    ScriptSig* sigA = script_sig_create(g_allocScratch, script_mask_bool, args, array_elems(args));
    ScriptSig* sigB = script_sig_clone(g_allocHeap, sigA);

    check_eq_int(script_sig_ret(sigA), script_sig_ret(sigB));
    check_eq_int(script_sig_arg_count(sigA), script_sig_arg_count(sigB));

    check_eq_string(script_sig_arg(sigA, 0).name, script_sig_arg(sigB, 0).name);
    check_eq_int(script_sig_arg(sigA, 0).mask, script_sig_arg(sigB, 0).mask);

    script_sig_destroy(sigA);
    script_sig_destroy(sigB);
  }

  it("can clone signatures with multiple arguments") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_num},
        {.name = string_lit("argB"), .mask = script_mask_null},
        {.name = string_lit("argC"), .mask = script_mask_null | script_mask_vec3},
    };
    ScriptSig* sigA = script_sig_create(g_allocScratch, script_mask_bool, args, array_elems(args));
    ScriptSig* sigB = script_sig_clone(g_allocHeap, sigA);

    check_eq_int(script_sig_ret(sigA), script_sig_ret(sigB));
    check_eq_int(script_sig_arg_count(sigA), script_sig_arg_count(sigB));

    for (u8 i = 0; i != array_elems(args); ++i) {
      check_eq_string(script_sig_arg(sigA, i).name, script_sig_arg(sigB, i).name);
      check_eq_int(script_sig_arg(sigA, i).mask, script_sig_arg(sigB, i).mask);
    }

    script_sig_destroy(sigA);
    script_sig_destroy(sigB);
  }

  it("can create a textual representation of a signature with zero arguments") {
    const ScriptMask ret = script_mask_null | script_mask_bool;
    ScriptSig*       sig = script_sig_create(g_allocScratch, ret, null, 0);

    check_eq_string(script_sig_scratch(sig), string_lit("() -> bool?"));

    script_sig_destroy(sig);
  }

  it("can create a textual representation of a signature argument") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_num},
    };
    ScriptSig* sig = script_sig_create(g_allocScratch, script_mask_any, args, array_elems(args));

    check_eq_string(script_sig_arg_scratch(sig, 0), string_lit("argA: num"));

    script_sig_destroy(sig);
  }

  it("can create a textual representation of a signature with one argument") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_num},
    };
    ScriptSig* sig = script_sig_create(g_allocScratch, script_mask_any, args, array_elems(args));

    check_eq_string(script_sig_scratch(sig), string_lit("(argA: num) -> any"));

    script_sig_destroy(sig);
  }

  it("can create a textual representation of a signature with multiple arguments") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_num},
        {.name = string_lit("argB"), .mask = script_mask_null},
        {
            .name  = string_lit("argC"),
            .mask  = script_mask_null | script_mask_vec3,
            .flags = ScriptSigArgFlags_Multi,
        },
    };
    ScriptSig* sig = script_sig_create(g_allocScratch, script_mask_any, args, array_elems(args));

    check_eq_string(
        script_sig_scratch(sig), string_lit("(argA: num, argB, argC: vec3?...) -> any"));

    script_sig_destroy(sig);
  }

  it("excludes empty masks from the textual representation") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_none},
        {.name = string_lit("argB"), .mask = script_mask_none},
        {.name = string_lit("argC"), .mask = script_mask_none, .flags = ScriptSigArgFlags_Multi},
    };
    ScriptSig* sig = script_sig_create(g_allocScratch, script_mask_none, args, array_elems(args));

    check_eq_string(script_sig_scratch(sig), string_lit("(argA, argB, argC...)"));

    script_sig_destroy(sig);
  }
}
