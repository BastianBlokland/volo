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

  setup() { doc = script_create(g_allocHeap); }

  it("can create value expressions") {
    check_expr_str_lit(doc, script_add_anon_value(doc, script_null()), "[value: null]");
    check_expr_str_lit(doc, script_add_anon_value(doc, script_num(42)), "[value: 42]");
    check_expr_str_lit(
        doc, script_add_anon_value(doc, script_vec3_lit(1, 2, 3)), "[value: 1, 2, 3]");
  }

  it("can create load expressions") {
    check_expr_str_lit(
        doc, script_add_anon_mem_load(doc, string_hash_lit("Hello")), "[mem-load: $938478706]");
  }

  it("can create store expressions") {
    check_expr_str_lit(
        doc,
        script_add_anon_mem_store(
            doc, string_hash_lit("Hello"), script_add_anon_value(doc, script_num(42))),
        "[mem-store: $938478706]\n"
        "  [value: 42]");
  }

  it("can create basic intrinsic expressions") {
    check_expr_str_lit(
        doc,
        script_add_anon_intrinsic(
            doc,
            ScriptIntrinsic_Vec3Compose,
            (const ScriptExpr[]){
                script_add_anon_value(doc, script_num(1)),
                script_add_anon_value(doc, script_num(2)),
                script_add_anon_value(doc, script_num(3)),
            }),
        "[intrinsic: vec3-compose]\n"
        "  [value: 1]\n"
        "  [value: 2]\n"
        "  [value: 3]");
  }

  it("can create nested intrinsic expressions") {
    check_expr_str_lit(
        doc,
        script_add_anon_intrinsic(
            doc,
            ScriptIntrinsic_Greater,
            (const ScriptExpr[]){
                script_add_anon_intrinsic(
                    doc,
                    ScriptIntrinsic_Equal,
                    (const ScriptExpr[]){
                        script_add_anon_value(doc, script_null()),
                        script_add_anon_value(doc, script_vec3_lit(1, 2, 3)),
                    }),
                script_add_anon_intrinsic(
                    doc,
                    ScriptIntrinsic_Negate,
                    (const ScriptExpr[]){
                        script_add_anon_value(doc, script_num(42)),
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
    const ScriptExpr expr = script_add_anon_intrinsic(
        doc,
        ScriptIntrinsic_Greater,
        (const ScriptExpr[]){
            script_add_anon_intrinsic(
                doc,
                ScriptIntrinsic_Equal,
                (const ScriptExpr[]){
                    script_add_anon_value(doc, script_null()),
                    script_add_anon_value(doc, script_vec3_lit(1, 2, 3)),
                }),
            script_add_anon_intrinsic(
                doc,
                ScriptIntrinsic_Negate,
                (const ScriptExpr[]){
                    script_add_anon_value(doc, script_num(42)),
                }),
        });
    CountVisitorContext ctx = {0};
    script_expr_visit(doc, expr, &ctx, &test_doc_count_visitor);
    check_eq_int(ctx.count, 6);
  }

  it("can test if expressions are static") {
    static const struct {
      String input;
      bool   isStatic;
    } g_testData[] = {
        {string_static("1"), .isStatic = true},
        {string_static("((1))"), .isStatic = true},
        {string_static("if(true) {2} else {}"), .isStatic = true},
        {string_static("1 + 2 + 3"), .isStatic = true},
        {string_static("true ? 1 + 2 : 3 + 4"), .isStatic = true},
        {string_static("while(false) {}"), .isStatic = true},
        {string_static("for(;;) {}"), .isStatic = true},
        {string_static("vec3(1, 2, 3)"), .isStatic = true},
        {string_static("distance(1 + 2, 3 / 4)"), .isStatic = true},

        {string_static("random()"), .isStatic = false},
        {string_static("random_between(1, 2)"), .isStatic = false},
        {string_static("random_sphere()"), .isStatic = false},
        {string_static("random_circle_xz()"), .isStatic = false},
        {string_static("return"), .isStatic = false},
        {string_static("return 42"), .isStatic = false},
        {string_static("assert(true)"), .isStatic = false},
        {string_static("while(true) { continue }"), .isStatic = false},
        {string_static("while(true) { break }"), .isStatic = false},
        {string_static("var i"), .isStatic = false},
        {string_static("var i; i"), .isStatic = false},
        {string_static("$hello"), .isStatic = false},
        {string_static("1 + 2 + $hello"), .isStatic = false},
        {string_static("$hello + $world"), .isStatic = false},
        {string_static("$hello = 42"), .isStatic = false},
        {string_static("1 + 2 + ($hello = 42)"), .isStatic = false},
        {string_static("($hello = 42) + ($world = 1337)"), .isStatic = false},
        {string_static("$hello + ($world = 42)"), .isStatic = false},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptBinder*    binder    = null;
      ScriptDiagBag*   diagsNull = null;
      ScriptSymBag*    symsNull  = null;
      const ScriptExpr expr = script_read(doc, binder, g_testData[i].input, diagsNull, symsNull);
      check_require(!sentinel_check(expr));

      check(script_expr_static(doc, expr) == g_testData[i].isStatic);
    }
  }

  it("can test if expressions are always truthy") {
    static const struct {
      String input;
      bool   isTruthy;
    } g_testData[] = {
        {string_static("1"), .isTruthy = true},
        {string_static("true"), .isTruthy = true},
        {string_static("2 > 1"), .isTruthy = true},
        {string_static("2 > 1 ? (1 < 2) : (2 > 3)"), .isTruthy = true},
        {string_static("distance(vec3(1,2,3), vec3(0,0,0)) > 0"), .isTruthy = true},

        {string_static("while(true) {}"), .isTruthy = false},
        {string_static("false"), .isTruthy = false},
        {string_static("null"), .isTruthy = false},
        {string_static("1 > 2"), .isTruthy = false},
        {string_static("random()"), .isTruthy = false},
        {string_static("return"), .isTruthy = false},
        {string_static("$i = true"), .isTruthy = false},
        {string_static("var i = true"), .isTruthy = false},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptBinder*    binder    = null;
      ScriptDiagBag*   diagsNull = null;
      ScriptSymBag*    symsNull  = null;
      const ScriptExpr expr = script_read(doc, binder, g_testData[i].input, diagsNull, symsNull);
      check_require(!sentinel_check(expr));

      check(script_expr_always_truthy(doc, expr) == g_testData[i].isTruthy);
    }
  }

  it("can check for always uncaught signals") {
    static const struct {
      String          input;
      ScriptDocSignal sig;
    } g_testData[] = {
        {string_static("1"), .sig = ScriptDocSignal_None},
        {string_static("return"), .sig = ScriptDocSignal_Return},
        {string_static("true ? return 0 : 0"), .sig = ScriptDocSignal_Return},
        {string_static("false ? return 0 : 0"), .sig = ScriptDocSignal_None},
        {string_static("true ? 0 : return 0"), .sig = ScriptDocSignal_None},
        {string_static("false ? 0 : return 0"), .sig = ScriptDocSignal_Return},
        {string_static("$i ? return 0 : return 1"), .sig = ScriptDocSignal_None},
        {string_static("(while(true) {}) ? return 0 : return 1"), .sig = ScriptDocSignal_None},
        {string_static("var i = { return }"), .sig = ScriptDocSignal_Return},
        {string_static("$i = { return }"), .sig = ScriptDocSignal_Return},
        {string_static("vec3(1,2,3)"), .sig = ScriptDocSignal_None},
        {string_static("vec3(1,return 2,3)"), .sig = ScriptDocSignal_Return},
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      ScriptBinder*    binder    = null;
      ScriptDiagBag*   diagsNull = null;
      ScriptSymBag*    symsNull  = null;
      const ScriptExpr expr = script_read(doc, binder, g_testData[i].input, diagsNull, symsNull);
      check_require(!sentinel_check(expr));

      check_eq_int(script_expr_always_uncaught_signal(doc, expr), g_testData[i].sig);
    }
  }

  teardown() { script_destroy(doc); }
}
