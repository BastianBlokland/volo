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

  it("supports integer type") {
#define X(_T_, _MIN_, _MAX_)                                                                       \
  const DataType type_##_T_ = data_prim_t(_T_);                                                    \
  test_jsonschema_write(                                                                           \
      _testCtx,                                                                                    \
      reg,                                                                                         \
      type_##_T_,                                                                                  \
      string_lit("{\n"                                                                             \
                 "  \"title\": \"" #_T_ "\",\n"                                                    \
                 "  \"type\": \"integer\",\n"                                                      \
                 "  \"minimum\": " #_MIN_ ",\n"                                                    \
                 "  \"maximum\": " #_MAX_ "\n"                                                     \
                 "}"));

    X(i8, -128, 127)
    X(i16, -32768, 32767)
    X(i32, -2147483648, 2147483647)
    X(i64, -9.2233720369e18, 9.2233720369e18)
    X(u8, 0, 255)
    X(u16, 0, 65535)
    X(u32, 0, 4294967295)
    X(u64, 0, 1.8446744074e19)
#undef X
  }

  it("supports float types") {
#define X(_T_)                                                                                     \
  const DataType type_##_T_ = data_prim_t(_T_);                                                    \
  test_jsonschema_write(                                                                           \
      _testCtx,                                                                                    \
      reg,                                                                                         \
      type_##_T_,                                                                                  \
      string_lit("{\n"                                                                             \
                 "  \"title\": \"" #_T_ "\",\n"                                                    \
                 "  \"type\": \"number\"\n"                                                        \
                 "}"));

    X(f32)
    X(f64)
#undef X
  }

  teardown() { data_reg_destroy(reg); }
}
