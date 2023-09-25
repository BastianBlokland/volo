#include "check_spec.h"
#include "core_alloc.h"
#include "script_binder.h"

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

  teardown() { script_binder_destroy(binder); }
}
