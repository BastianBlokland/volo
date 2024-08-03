#include "check_spec.h"
#include "core_alloc.h"
#include "data_registry.h"
#include "data_schema.h"

static void test_jsonschema_write(
    CheckTestContext* _testCtx, const DataReg* reg, const DataMeta meta, const String expected) {

  Mem       buffer    = mem_stack(1024);
  DynString dynString = dynstring_create_over(buffer);
  data_jsonschema_write(reg, &dynString, meta, DataJsonSchemaFlags_None);

  check_eq_string(dynstring_view(&dynString), expected);
}

spec(jsonschema) {

  DataReg* reg = null;

  setup() { reg = data_reg_create(g_allocHeap); }

  it("supports a boolean type") {
    const DataMeta meta = data_meta_t(data_prim_t(bool));

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"title\": \"bool\",\n"
                   "  \"type\": \"boolean\"\n"
                   "}"));
  }

  it("supports integer type") {
#define X(_T_, _MIN_, _MAX_)                                                                       \
  const DataMeta meta_##_T_ = data_meta_t(data_prim_t(_T_));                                       \
  test_jsonschema_write(                                                                           \
      _testCtx,                                                                                    \
      reg,                                                                                         \
      meta_##_T_,                                                                                  \
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
  const DataMeta meta_##_T_ = data_meta_t(data_prim_t(_T_));                                       \
  test_jsonschema_write(                                                                           \
      _testCtx,                                                                                    \
      reg,                                                                                         \
      meta_##_T_,                                                                                  \
      string_lit("{\n"                                                                             \
                 "  \"title\": \"" #_T_ "\",\n"                                                    \
                 "  \"type\": \"number\"\n"                                                        \
                 "}"));

    X(f32)
    X(f64)
#undef X
  }

  it("supports a string") {
    const DataMeta meta = data_meta_t(data_prim_t(String));

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"title\": \"String\",\n"
                   "  \"type\": \"string\"\n"
                   "}"));
  }

  it("supports a non-empty string") {
    const DataMeta meta = data_meta_t(data_prim_t(String), .flags = DataFlags_NotEmpty);

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"title\": \"String\",\n"
                   "  \"type\": \"string\",\n"
                   "  \"minLength\": 1\n"
                   "}"));
  }

  it("supports raw memory") {
    const DataMeta meta = data_meta_t(data_prim_t(DataMem));

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"title\": \"DataMem\",\n"
                   "  \"type\": \"string\",\n"
                   "  \"contentEncoding\": \"base64\"\n"
                   "}"));
  }

  it("supports optional pointer") {
    const DataMeta meta = data_meta_t(data_prim_t(String), .container = DataContainer_Pointer);

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"anyOf\": [\n"
                   "    {\n"
                   "      \"title\": \"String\",\n"
                   "      \"type\": \"string\"\n"
                   "    },\n"
                   "    {\n"
                   "      \"const\": null,\n"
                   "      \"title\": \"String\"\n"
                   "    }\n"
                   "  ]\n"
                   "}"));
  }

  it("supports required pointer") {
    const DataMeta meta = data_meta_t(
        data_prim_t(String), .container = DataContainer_Pointer, .flags = DataFlags_NotEmpty);

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"title\": \"String\",\n"
                   "  \"type\": \"string\"\n"
                   "}"));
  }

  it("supports arrays") {
    const DataMeta meta = data_meta_t(data_prim_t(String), .container = DataContainer_Array);

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"type\": \"array\",\n"
                   "  \"items\": {\n"
                   "    \"title\": \"String\",\n"
                   "    \"type\": \"string\"\n"
                   "  }\n"
                   "}"));
  }

  it("supports non-empty arrays") {
    const DataMeta meta = data_meta_t(
        data_prim_t(String), .container = DataContainer_Array, .flags = DataFlags_NotEmpty);

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"type\": \"array\",\n"
                   "  \"minItems\": 1,\n"
                   "  \"items\": {\n"
                   "    \"title\": \"String\",\n"
                   "    \"type\": \"string\"\n"
                   "  }\n"
                   "}"));
  }

  it("supports enums") {
    enum TestEnum {
      TestEnum_A = -42,
      TestEnum_B = 42,
      TestEnum_C = 1337,
    };

    data_reg_enum_t(reg, TestEnum);
    data_reg_const_t(reg, TestEnum, A);
    data_reg_const_t(reg, TestEnum, B);
    data_reg_const_t(reg, TestEnum, C);

    const DataMeta meta = data_meta_t(t_TestEnum);

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"title\": \"TestEnum\",\n"
                   "  \"$ref\": \"#/$defs/TestEnum\",\n"
                   "  \"$defs\": {\n"
                   "    \"TestEnum\": {\n"
                   "      \"enum\": [\n"
                   "        \"A\",\n"
                   "        \"B\",\n"
                   "        \"C\"\n"
                   "      ]\n"
                   "    }\n"
                   "  }\n"
                   "}"));
  }

  it("supports multi enums") {
    enum TestEnumFlags {
      TestEnumFlags_A = 1 << 0,
      TestEnumFlags_B = 1 << 1,
      TestEnumFlags_C = 1 << 2,
    };

    data_reg_enum_multi_t(reg, TestEnumFlags);
    data_reg_const_t(reg, TestEnumFlags, A);
    data_reg_const_t(reg, TestEnumFlags, B);
    data_reg_const_t(reg, TestEnumFlags, C);

    const DataMeta meta = data_meta_t(t_TestEnumFlags);

    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"title\": \"TestEnumFlags\",\n"
                   "  \"$ref\": \"#/$defs/TestEnumFlags\",\n"
                   "  \"$defs\": {\n"
                   "    \"TestEnumFlags\": {\n"
                   "      \"type\": \"array\",\n"
                   "      \"items\": {\n"
                   "        \"enum\": [\n"
                   "          \"A\",\n"
                   "          \"B\",\n"
                   "          \"C\"\n"
                   "        ]\n"
                   "      }\n"
                   "    }\n"
                   "  }\n"
                   "}"));
  }

  it("supports structures") {
    typedef struct {
      bool   valA;
      String valB;
      f64    valC;
    } TestStruct;

    data_reg_struct_t(reg, TestStruct);
    data_reg_field_t(reg, TestStruct, valA, data_prim_t(bool));
    data_reg_field_t(reg, TestStruct, valB, data_prim_t(String));
    data_reg_field_t(reg, TestStruct, valC, data_prim_t(f64));

    const DataMeta meta = data_meta_t(t_TestStruct);

    // clang-format off
    test_jsonschema_write(
        _testCtx,
        reg,
        meta,
        string_lit("{\n"
                   "  \"title\": \"TestStruct\",\n"
                   "  \"$ref\": \"#/$defs/TestStruct\",\n"
                   "  \"$defs\": {\n"
                   "    \"TestStruct\": {\n"
                   "      \"type\": \"object\",\n"
                   "      \"additionalProperties\": false,\n"
                   "      \"properties\": {\n"
                   "        \"valA\": {\n"
                   "          \"title\": \"bool\",\n"
                   "          \"type\": \"boolean\"\n"
                   "        },\n"
                   "        \"valB\": {\n"
                   "          \"title\": \"String\",\n"
                   "          \"type\": \"string\"\n"
                   "        },\n"
                   "        \"valC\": {\n"
                   "          \"title\": \"f64\",\n"
                   "          \"type\": \"number\"\n"
                   "        }\n"
                   "      },\n"
                   "      \"required\": [\n"
                   "        \"valA\",\n"
                   "        \"valB\",\n"
                   "        \"valC\"\n"
                   "      ],\n"
                   "      \"defaultSnippets\": [\n"
                   "        {\n"
                   "          \"label\": \"New\",\n"
                   "          \"body\": \"^{\\n  \\\"valA\\\": false,\\n  \\\"valB\\\": \\\"\\\",\\n  \\\"valC\\\": 0\\n}\"\n"
                   "        }\n"
                   "      ]\n"
                   "    }\n"
                   "  }\n"
                   "}"));
    // clang-format on
  }

  teardown() { data_reg_destroy(reg); }
}
