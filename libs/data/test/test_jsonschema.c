#include "check_spec.h"
#include "core_alloc.h"
#include "data_schema.h"

static void test_jsonschema_write(
    CheckTestContext* _testCtx, const DataReg* reg, const DataType type, const String expected) {

  Mem       buffer    = mem_stack(1024);
  DynString dynString = dynstring_create_over(buffer);
  data_jsonschema_write(reg, &dynString, type);

  check_eq_string(dynstring_view(&dynString), expected);
}

spec(jsonschema) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_alloc_heap); }

  it("supports a boolean type") {
    const DataType type = data_prim_t(bool);

    test_jsonschema_write(
        _testCtx,
        reg,
        type,
        string_lit("{\n"
                   "  \"title\": \"bool\",\n"
                   "  \"type\": \"boolean\"\n"
                   "}"));
  }

  teardown() { data_reg_destroy(reg); }
}
