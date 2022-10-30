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
    check_expr_str(
        doc,
        script_add_compare(
            doc,
            script_add_lit(doc, script_number(1)),
            script_add_lit(doc, script_number(2)),
            ScriptComparison_Greater),
        "[compare: greater]\n"
        "  [lit: 1]\n"
        "  [lit: 2]");
  }

  it("can create nested compare expressions") {
    check_expr_str(
        doc,
        script_add_compare(
            doc,
            script_add_compare(
                doc,
                script_add_lit(doc, script_null()),
                script_add_lit(doc, script_vector3_lit(1, 2, 3)),
                ScriptComparison_Equal),
            script_add_compare(
                doc,
                script_add_lit(doc, script_number(1)),
                script_add_lit(doc, script_entity(0x42)),
                ScriptComparison_Less),
            ScriptComparison_Greater),
        "[compare: greater]\n"
        "  [compare: equal]\n"
        "    [lit: null]\n"
        "    [lit: 1, 2, 3]\n"
        "  [compare: less]\n"
        "    [lit: 1]\n"
        "    [lit: 42]");
  }

  teardown() { script_destroy(doc); }
}
