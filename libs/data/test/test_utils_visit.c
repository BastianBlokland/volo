#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "data_registry.h"
#include "data_utils.h"

typedef struct {
  String txt;
} TestVisitStructA;

typedef struct {
  TestVisitStructA  value;
  TestVisitStructA* ptr;
  struct {
    TestVisitStructA* values;
    usize             count;
  } array;
} TestVisitStructB;

typedef struct {
  u32 countA, countB;
} TestVisitContext;

static void test_data_visitor(void* ctx, const Mem data) {
  TestVisitContext* visitCtx  = ctx;
  TestVisitStructA* dataTyped = mem_as_t(data, TestVisitStructA);
  if (string_eq(dataTyped->txt, string_lit("a"))) {
    ++visitCtx->countA;
  } else if (string_eq(dataTyped->txt, string_lit("b"))) {
    ++visitCtx->countB;
  }
}

spec(utils_visit) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("can visit structures") {

    data_reg_struct_t(reg, TestVisitStructA);
    data_reg_field_t(reg, TestVisitStructA, txt, data_prim_t(String));

    data_reg_struct_t(reg, TestVisitStructB);
    data_reg_field_t(reg, TestVisitStructB, value, t_TestVisitStructA);
    data_reg_field_t(
        reg, TestVisitStructB, ptr, t_TestVisitStructA, .container = DataContainer_Pointer);
    data_reg_field_t(
        reg, TestVisitStructB, array, t_TestVisitStructA, .container = DataContainer_HeapArray);

    TestVisitStructA ptrValue = {
        .txt = string_lit("a"),
    };

    TestVisitStructA arrayValues[] = {
        {.txt = string_lit("b")},
        {.txt = string_lit("a")},
        {.txt = string_lit("b")},
    };

    const TestVisitStructB val = {
        .value = {.txt = string_lit("a")},
        .ptr   = &ptrValue,
        .array = {.values = arrayValues, .count = array_elems(arrayValues)},
    };

    TestVisitContext ctx = {0};
    data_visit(
        reg,
        data_meta_t(t_TestVisitStructB),
        mem_var(val),
        t_TestVisitStructA,
        &ctx,
        test_data_visitor);

    check_eq_int(ctx.countA, 3);
    check_eq_int(ctx.countB, 2);
  }

  teardown() { data_reg_destroy(reg); }
}
