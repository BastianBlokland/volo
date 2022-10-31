#include "check_spec.h"
#include "core_alloc.h"
#include "script_doc.h"

#include "utils_internal.h"

spec(doc) {

  ScriptDoc* doc = null;

  setup() { doc = script_create(g_alloc_heap); }

  it("can create value expressions") {
    check_expr_str_lit(doc, script_add_value(doc, script_null()), "[value: null]");
    check_expr_str_lit(doc, script_add_value(doc, script_number(42)), "[value: 42]");
    check_expr_str_lit(doc, script_add_value(doc, script_vector3_lit(1, 2, 3)), "[value: 1, 2, 3]");
  }

  it("can create load expressions") {
    check_expr_str_lit(doc, script_add_load(doc, string_hash_lit("Hello")), "[load: $938478706]");
  }

  it("can create basic unary operation expressions") {
    check_expr_str_lit(
        doc,
        script_add_op_unary(doc, script_add_value(doc, script_number(42)), ScriptOpUnary_Negate),
        "[op-unary: negate]\n"
        "  [value: 42]");
  }

  it("can create basic binary operation expressions") {
    check_expr_str_lit(
        doc,
        script_add_op_binary(
            doc,
            script_add_value(doc, script_number(1)),
            script_add_value(doc, script_number(2)),
            ScriptOpBinary_Greater),
        "[op-binary: greater]\n"
        "  [value: 1]\n"
        "  [value: 2]");
  }

  it("can create nested operation expressions") {
    check_expr_str_lit(
        doc,
        script_add_op_binary(
            doc,
            script_add_op_binary(
                doc,
                script_add_value(doc, script_null()),
                script_add_value(doc, script_vector3_lit(1, 2, 3)),
                ScriptOpBinary_Equal),
            script_add_op_unary(
                doc, script_add_value(doc, script_number(42)), ScriptOpUnary_Negate),
            ScriptOpBinary_Greater),
        "[op-binary: greater]\n"
        "  [op-binary: equal]\n"
        "    [value: null]\n"
        "    [value: 1, 2, 3]\n"
        "  [op-unary: negate]\n"
        "    [value: 42]");
  }

  teardown() { script_destroy(doc); }
}
