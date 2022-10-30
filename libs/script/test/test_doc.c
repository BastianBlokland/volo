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

  it("can create basic compare expressions") {
    const ScriptExpr comp = script_add_compare(
        doc,
        script_add_lit(doc, script_number(1)),
        script_add_lit(doc, script_number(2)),
        ScriptComparison_Greater);
    check_expr_str(
        doc,
        comp,
        "[compare: greater]\n"
        "[lit: 1]\n"
        "[lit: 2]");
  }

  teardown() { script_destroy(doc); }
}
