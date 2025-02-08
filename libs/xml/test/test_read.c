#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "xml_doc.h"
#include "xml_read.h"

spec(read) {

  XmlDoc* doc = null;

  setup() { doc = xml_create(g_allocHeap, 0); }

  it("can read arrays") {
    XmlNode tmpValA, tmpValB, tmpValC, tmpValD, tmpValE;
    struct {
      String  input;
      XmlNode expected;
    } const data[] = {
        {string_lit("[]"), xml_add_array(doc)},
        {string_lit("[  ]"), xml_add_array(doc)},
        {string_lit("[\n\n\n]"), xml_add_array(doc)},
        {string_lit("[true]"),
         (tmpValA = xml_add_array(doc),
          xml_add_elem(doc, tmpValA, xml_add_bool(doc, true)),
          tmpValA)},
        {string_lit("[true,]"),
         (tmpValB = xml_add_array(doc),
          xml_add_elem(doc, tmpValB, xml_add_bool(doc, true)),
          tmpValB)},
        {string_lit("[1,2,3]"),
         (tmpValC = xml_add_array(doc),
          xml_add_elem(doc, tmpValC, xml_add_number(doc, 1.0)),
          xml_add_elem(doc, tmpValC, xml_add_number(doc, 2.0)),
          xml_add_elem(doc, tmpValC, xml_add_number(doc, 3.0)),
          tmpValC)},
        {string_lit("[[],\n[\n]\n,[]]"),
         (tmpValD = xml_add_array(doc),
          xml_add_elem(doc, tmpValD, xml_add_array(doc)),
          xml_add_elem(doc, tmpValD, xml_add_array(doc)),
          xml_add_elem(doc, tmpValD, xml_add_array(doc)),
          tmpValD)},
        {string_lit("[[],true,null]"),
         (tmpValE = xml_add_array(doc),
          xml_add_elem(doc, tmpValE, xml_add_array(doc)),
          xml_add_elem(doc, tmpValE, xml_add_bool(doc, true)),
          xml_add_elem(doc, tmpValE, xml_add_null(doc)),
          tmpValE)},
    };

    JsonResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      const String rem = xml_read(doc, data[i].input, JsonReadFlags_None, &res);

      check(string_is_empty(rem));
      check_require(res.type == JsonResultType_Success);
      check(xml_eq(doc, res.val, data[i].expected));
    }
  }

  it("can read objects") {
    XmlNode tmpValA, tmpValB, tmpValC, tmpValD, tmpValE, tmpValF;
    struct {
      String  input;
      XmlNode expected;
    } const data[] = {
        {string_lit("{}"), xml_add_object(doc)},
        {string_lit("{  }"), xml_add_object(doc)},
        {string_lit("{\n\n\n}"), xml_add_object(doc)},
        {string_lit("{\"a\":true}"),
         (tmpValA = xml_add_object(doc),
          xml_add_field_str(doc, tmpValA, string_lit("a"), xml_add_bool(doc, true)),
          tmpValA)},
        {string_lit("{\"a\":true,}"),
         (tmpValB = xml_add_object(doc),
          xml_add_field_str(doc, tmpValB, string_lit("a"), xml_add_bool(doc, true)),
          tmpValB)},
        {string_lit("{\n\"a\"\n:\ntrue\n}"),
         (tmpValC = xml_add_object(doc),
          xml_add_field_str(doc, tmpValC, string_lit("a"), xml_add_bool(doc, true)),
          tmpValC)},
        {string_lit("{\"a\":1,\"b\":2,\"c\":3}"),
         (tmpValD = xml_add_object(doc),
          xml_add_field_str(doc, tmpValD, string_lit("a"), xml_add_number(doc, 1.0)),
          xml_add_field_str(doc, tmpValD, string_lit("b"), xml_add_number(doc, 2.0)),
          xml_add_field_str(doc, tmpValD, string_lit("c"), xml_add_number(doc, 3.0)),
          tmpValD)},
        {string_lit("{\"a\":{},\"b\":{}}"),
         (tmpValE = xml_add_object(doc),
          xml_add_field_str(doc, tmpValE, string_lit("a"), xml_add_object(doc)),
          xml_add_field_str(doc, tmpValE, string_lit("b"), xml_add_object(doc)),
          tmpValE)},
        {string_lit("{\"你好世界\":true}"),
         (tmpValF = xml_add_object(doc),
          xml_add_field_str(doc, tmpValF, string_lit("你好世界"), xml_add_bool(doc, true)),
          tmpValF)},
    };

    JsonResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      const String rem = xml_read(doc, data[i].input, JsonReadFlags_None, &res);

      check(string_is_empty(rem));
      check_require(res.type == JsonResultType_Success);
      check(xml_eq(doc, res.val, data[i].expected));
    }
  }

  it("can read numbers") {
    struct {
      String input;
      f64    expected;
    } const data[] = {
        {string_lit("0"), 0.0},
        {string_lit("1"), 1.0},
        {string_lit("-1"), -1.0},
        {string_lit("42.0"), 42.0},
        {string_lit("42.1337"), 42.1337},
        {string_lit("-42.1337"), -42.1337},
        {string_lit(".1"), 0.1},
        {string_lit("1.1e12"), 1.1e12},
        {string_lit("1.1E12"), 1.1E12},
        {string_lit("1.1E-12"), 1.1E-12},
        {string_lit("1.1E+12"), 1.1E+12},
        {string_lit("99.99e0"), 99.99e0},
        {string_lit("99.99e1"), 99.99e1},
    };

    JsonResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      const String rem = xml_read(doc, data[i].input, JsonReadFlags_None, &res);

      check(string_is_empty(rem));
      check_require(res.type == JsonResultType_Success);
      check_eq_float(xml_number(doc, res.val), data[i].expected, 1e-32);
    }
  }

  it("can read strings") {
    struct {
      String input;
      String expected;
    } const data[] = {
        {string_lit("\"\""), string_empty},
        {string_lit("\"Hello\""), string_lit("Hello")},
        {string_lit("\"Hello World\""), string_lit("Hello World")},
        {string_lit("\"Hello\\nWorld\""), string_lit("Hello\nWorld")},
        {string_lit("\"Hello\\u000AWorld\""), string_lit("Hello\nWorld")},
        {string_lit("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\""), string_lit("\"\\/\b\f\n\r\t")},
        {string_lit("\"\\u0026\""), string_lit("&")},
        {string_lit("\"\\U0026\""), string_lit("&")},
        {string_lit("\"\\u039B\""), string_lit("Λ")},
        {string_lit("\"\\u0E3F\""), string_lit("฿")},
        {string_lit("\"\\u1D459\""), string_lit("𝑙")},
        {string_lit("\"\\u41\""), string_lit("A")},
        {string_lit("\"\\u0041\""), string_lit("A")},
        {string_lit("\"\\ug\""), string_lit("\0g")},
        {string_lit("\"你好世界\""), string_lit("你好世界")},
    };

    JsonResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      const String rem = xml_read(doc, data[i].input, JsonReadFlags_None, &res);

      check(string_is_empty(rem));
      check_require(res.type == JsonResultType_Success);
      check_eq_string(xml_string(doc, res.val), data[i].expected);
    }
  }

  it("can read booleans") {
    struct {
      String input;
      bool   expected;
    } const data[] = {
        {string_lit("true"), true},
        {string_lit("false"), false},
    };

    JsonResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      const String rem = xml_read(doc, data[i].input, JsonReadFlags_None, &res);

      check(string_is_empty(rem));
      check_require(res.type == JsonResultType_Success);
      check(xml_bool(doc, res.val) == data[i].expected);
    }
  }

  it("can read null") {
    JsonResult   res;
    const String rem = xml_read(doc, string_lit("null"), JsonReadFlags_None, &res);

    check(string_is_empty(rem));
    check_require(res.type == JsonResultType_Success);
    check_eq_int(xml_type(doc, res.val), JsonType_Null);
  }

  it("can read sequences of multiple values") {
    String input = string_lit("1 true null [] {}");

    DynArray values = dynarray_create_over_t(mem_stack(256), XmlNode);

    while (!string_is_empty(input)) {
      JsonResult res;
      input = xml_read(doc, input, JsonReadFlags_None, &res);
      check_require(res.type == JsonResultType_Success);
      *dynarray_push_t(&values, XmlNode) = res.val;
    }

    dynarray_destroy(&values);

    check_eq_int(xml_type(doc, *dynarray_at_t(&values, 0, XmlNode)), JsonType_Number);
    check_eq_int(xml_type(doc, *dynarray_at_t(&values, 1, XmlNode)), JsonType_Bool);
    check_eq_int(xml_type(doc, *dynarray_at_t(&values, 2, XmlNode)), JsonType_Null);
    check_eq_int(xml_type(doc, *dynarray_at_t(&values, 3, XmlNode)), JsonType_Array);
    check_eq_int(xml_type(doc, *dynarray_at_t(&values, 4, XmlNode)), JsonType_Object);
  }

  it("fails on invalid input") {
    struct {
      String input;
      String error;
    } const data[] = {
        {string_empty, string_lit("Truncated")},
        {string_lit("^"), string_lit("InvalidChar")},
        {string_lit("Hello"), string_lit("InvalidChar")},
        {string_lit("fAlse"), string_lit("InvalidCharInFalse")},
        {string_lit("tRue"), string_lit("InvalidCharInTrue")},
        {string_lit("\"\\$\""), string_lit("InvalidEscapeSequence")},
        {string_lit("\"\\$\""), string_lit("InvalidEscapeSequence")},
        {string_lit("\"\\N\""), string_lit("InvalidEscapeSequence")},
        {string_lit("\"Hello\nWorld\""), string_lit("InvalidCharInString")},
        {string_lit("\""), string_lit("UnterminatedString")},
        {string_lit("{1}"), string_lit("InvalidFieldName")},
        {string_lit("{,}"), string_lit("InvalidFieldName")},
        {string_lit("{\"a\"1}"), string_lit("InvalidFieldSeparator")},
        {string_lit("[,]"), string_lit("UnexpectedToken")},
        {string_lit("["), string_lit("Truncated")},
        {string_lit("{"), string_lit("Truncated")},
        {string_lit("[1 1]"), string_lit("UnexpectedToken")},
        {string_lit("{\"a\":1 \"b\"}"), string_lit("UnexpectedToken")},
        {string_lit("[^]"), string_lit("InvalidChar")},
        {string_lit("{\"a\":^}"), string_lit("InvalidChar")},
        {string_lit("{\"a\":1,\"a\":2}"), string_lit("DuplicateField")},
        {string_lit("{\"\":1}"), string_lit("InvalidFieldName")},
    };

    JsonResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      xml_read(doc, data[i].input, JsonReadFlags_None, &res);

      check_require(res.type == JsonResultType_Fail);
      check_eq_string(xml_error_str(res.error), data[i].error);
    }
  }

  it("fails when input contains a too long string") {
    DynString input = dynstring_create(g_allocHeap, usize_kibibyte * 512);
    dynstring_append_char(&input, '"');
    dynstring_append_chars(&input, 'a', usize_kibibyte * 384);
    dynstring_append_char(&input, '"');

    JsonResult res;
    xml_read(doc, dynstring_view(&input), JsonReadFlags_None, &res);

    dynstring_destroy(&input);

    check_require(res.type == JsonResultType_Fail);
    check_eq_string(xml_error_str(res.error), string_lit("TooLongString"));
  }

  it("fails when input contains too deep nesting") {
    DynString input = dynstring_create_over(mem_stack(256));
    dynstring_append_chars(&input, '[', 101);
    dynstring_append_chars(&input, ']', 101);

    JsonResult res;
    xml_read(doc, dynstring_view(&input), JsonReadFlags_None, &res);

    dynstring_destroy(&input);

    check_require(res.type == JsonResultType_Fail);
    check_eq_string(xml_error_str(res.error), string_lit("MaximumDepthExceeded"));
  }

  teardown() { xml_destroy(doc); }
}
