#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_sig.h"
#include "script_val.h"

spec(sig) {
  it("can store ret type") {
    ScriptSig* sig = script_sig_create(g_alloc_scratch, script_mask_bool, null, 0);

    check_eq_int(script_sig_ret(sig), script_mask_bool);

    script_sig_destroy(sig);
  }

  it("can store ret type and arguments") {
    const ScriptSigArg args[] = {
        {.name = string_lit("argA"), .mask = script_mask_number},
        {.name = string_lit("argB"), .mask = script_mask_null},
        {.name = string_lit("argC"), .mask = script_mask_null | script_mask_vector3},
    };
    ScriptSig* sig = script_sig_create(g_alloc_scratch, script_mask_bool, args, array_elems(args));

    check_eq_int(script_sig_ret(sig), script_mask_bool);
    check_eq_int(script_sig_arg_count(sig), array_elems(args));
    for (u8 i = 0; i != array_elems(args); ++i) {
      check_eq_string(script_sig_arg(sig, i).name, args[i].name);
      check_eq_int(script_sig_arg(sig, i).mask, args[i].mask);
    }

    script_sig_destroy(sig);
  }
}
