#include "check_spec.h"
#include "core_alloc.h"
#include "script_doc.h"

#include "utils_internal.h"

spec(doc) {

  ScriptDoc* doc = null;

  setup() { doc = script_create(g_alloc_heap); }

  it("can create literal expressions") {
    check_expr_str(doc, script_add_lit(doc, script_null()), "[lit: null]");
    check_expr_str(doc, script_add_lit(doc, script_number(42)), "[lit: 42]");
    check_expr_str(doc, script_add_lit(doc, script_vector3_lit(1, 2, 3)), "[lit: 1, 2, 3]");
  }

  teardown() { script_destroy(doc); }
}
