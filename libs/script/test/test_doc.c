#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_doc.h"
#include "script_read.h"

#include "utils_internal.h"

typedef struct {
  u32 count;
} CountVisitorContext;

static void test_doc_count_visitor(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  CountVisitorContext* countCtx = ctx;

  (void)doc;
  (void)expr;

  ++countCtx->count;
}

spec(doc) {

  ScriptDoc* doc = null;

  setup() { doc = script_create(g_alloc_heap); }

  it("can create value expressions") {
    check_expr_str_lit(doc, script_add_value(doc, script_null()), "[value: null]");
    check_expr_str_lit(doc, script_add_value(doc, script_number(42)), "[value: 42]");
    check_expr_str_lit(doc, script_add_value(doc, script_vector3_lit(1, 2, 3)), "[value: 1, 2, 3]");
  }

  it("can create load expressions") {
    check_expr_str_lit(
        doc, script_add_mem_load(doc, string_hash_lit("Hello")), "[mem-load: $938478706]");
  }

  it("can create store expressions") {
    check_expr_str_lit(
        doc,
        script_add_mem_store(
            doc, string_hash_lit("Hello"), script_add_value(doc, script_number(42))),
        "[mem-store: $938478706]\n"
        "  [value: 42]");
  }

  it("can create basic intrinsic expressions") {
    check_expr_str_lit(
        doc,
        script_add_intrinsic(
            doc,
            ScriptIntrinsic_Vector3Compose,
            (const ScriptExpr[]){
                script_add_value(doc, script_number(1)),
                script_add_value(doc, script_number(2)),
                script_add_value(doc, script_number(3)),
            }),
        "[intrinsic: vector3-compose]\n"
        "  [value: 1]\n"
        "  [value: 2]\n"
        "  [value: 3]");
  }

  it("can create nested intrinsic expressions") {
    check_expr_str_lit(
        doc,
        script_add_intrinsic(
            doc,
            ScriptIntrinsic_Greater,
            (const ScriptExpr[]){
                script_add_intrinsic(
                    doc,
                    ScriptIntrinsic_Equal,
                    (const ScriptExpr[]){
                        script_add_value(doc, script_null()),
                        script_add_value(doc, script_vector3_lit(1, 2, 3)),
                    }),
                script_add_intrinsic(
                    doc,
                    ScriptIntrinsic_Negate,
                    (const ScriptExpr[]){
                        script_add_value(doc, script_number(42)),
                    }),
            }),
        "[intrinsic: greater]\n"
        "  [intrinsic: equal]\n"
        "    [value: null]\n"
        "    [value: 1, 2, 3]\n"
        "  [intrinsic: negate]\n"
        "    [value: 42]");
  }

  it("can visit expressions") {
    const ScriptExpr expr = script_add_intrinsic(
        doc,
        ScriptIntrinsic_Greater,
        (const ScriptExpr[]){
            script_add_intrinsic(
                doc,
                ScriptIntrinsic_Equal,
                (const ScriptExpr[]){
                    script_add_value(doc, script_null()),
                    script_add_value(doc, script_vector3_lit(1, 2, 3)),
                }),
            script_add_intrinsic(
                doc,
                ScriptIntrinsic_Negate,
                (const ScriptExpr[]){
                    script_add_value(doc, script_number(42)),
                }),
        });
    CountVisitorContext ctx = {0};
    script_expr_visit(doc, expr, &ctx, &test_doc_count_visitor);
    check_eq_int(ctx.count, 6);
  }

  it("can test if expressions are readonly") {
    static const struct {
      String input;
      bool   readonly;
    } g_testData[] = {
        {string_static("1"), .readonly = true},
        {string_static("1 + 2 + 3"), .readonly = true},
        {string_static("$hello"), .readonly = true},
        {string_static("1 + 2 + $hello"), .readonly = true},
        {string_static("$hello + $world"), .readonly = true},

        {string_static("$hello = 42"), .readonly = false},
        {string_static("1 + 2 + ($hello = 42)"), .readonly = false},
        {string_static("($hello = 42) + ($world = 1337)"), .readonly = false},
        {string_static("$hello + ($world = 42)"), .readonly = false},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptBinder*    binder = null;
      ScriptReadResult readRes;
      script_read(doc, binder, g_testData[i].input, &readRes);
      check_require(readRes.type == ScriptResult_Success);

      check(script_expr_readonly(doc, readRes.expr) == g_testData[i].readonly);
    }
  }

  teardown() { script_destroy(doc); }
}
