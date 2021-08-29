#include "core_array.h"
#include "core_dynstring.h"

#include "json_write.h"

#include "check_spec.h"

spec(write) {

  JsonDoc*  doc    = null;
  DynString buffer = {0};

  setup() {
    doc    = json_create(g_alloc_heap, 0);
    buffer = dynstring_create(g_alloc_page, usize_kibibyte * 4);
  }

  it("can write arrays") {
    const JsonVal val = json_add_array(doc);
    json_add_elem(doc, val, json_add_null(doc));
    json_add_elem(doc, val, json_add_bool(doc, true));
    json_add_elem(doc, val, json_add_string(doc, string_lit("Hello")));

    json_write(&buffer, doc, val, &json_write_opts(.flags = JsonWriteFlags_None));
    check_eq_string(dynstring_view(&buffer), string_lit("[null,true,\"Hello\"]"));
  }

  it("can write arrays in pretty format") {
    const JsonVal val = json_add_array(doc);
    json_add_elem(doc, val, json_add_null(doc));
    json_add_elem(doc, val, json_add_bool(doc, true));
    json_add_elem(doc, val, json_add_string(doc, string_lit("Hello")));

    json_write(&buffer, doc, val, &json_write_opts(.flags = JsonWriteFlags_Pretty));
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("[\n"
                   "  null,\n"
                   "  true,\n"
                   "  \"Hello\"\n"
                   "]"));
  }

  it("can write objects") {
    const JsonVal val = json_add_object(doc);
    json_add_field_str(doc, val, string_lit("a"), json_add_null(doc));
    json_add_field_str(doc, val, string_lit("b"), json_add_bool(doc, true));
    json_add_field_str(doc, val, string_lit("c"), json_add_string(doc, string_lit("Hello")));

    json_write(&buffer, doc, val, &json_write_opts(.flags = JsonWriteFlags_None));
    check_eq_string(dynstring_view(&buffer), string_lit("{\"a\":null,\"b\":true,\"c\":\"Hello\"}"));
  }

  it("can write objects in pretty format") {
    const JsonVal val = json_add_object(doc);
    json_add_field_str(doc, val, string_lit("a"), json_add_null(doc));
    json_add_field_str(doc, val, string_lit("b"), json_add_bool(doc, true));
    json_add_field_str(doc, val, string_lit("c"), json_add_string(doc, string_lit("Hello")));

    json_write(&buffer, doc, val, &json_write_opts(.flags = JsonWriteFlags_Pretty));
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("{\n"
                   "  \"a\": null,\n"
                   "  \"b\": true,\n"
                   "  \"c\": \"Hello\"\n"
                   "}"));
  }

  it("can write strings") {
    const JsonVal val = json_add_string(doc, string_lit("Hello World"));

    json_write(&buffer, doc, val, &json_write_opts(.flags = JsonWriteFlags_None));
    check_eq_string(dynstring_view(&buffer), string_lit("\"Hello World\""));
  }

  it("can write strings with escape sequences") {
    const JsonVal val = json_add_string(doc, string_lit("\bHello\tWorld\n"));

    json_write(&buffer, doc, val, &json_write_opts(.flags = JsonWriteFlags_None));
    check_eq_string(dynstring_view(&buffer), string_lit("\"\\bHello\\tWorld\\n\""));
  }

  it("can write numbers") {
    const JsonVal val = json_add_number(doc, 42.1337);

    json_write(&buffer, doc, val, &json_write_opts(.flags = JsonWriteFlags_None));
    check_eq_string(dynstring_view(&buffer), string_lit("42.1337"));
  }

  it("can write true") {
    const JsonVal trueVal = json_add_bool(doc, true);

    json_write(&buffer, doc, trueVal, &json_write_opts(.flags = JsonWriteFlags_None));
    check_eq_string(dynstring_view(&buffer), string_lit("true"));
  }

  it("can write false") {
    const JsonVal trueVal = json_add_bool(doc, false);

    json_write(&buffer, doc, trueVal, &json_write_opts(.flags = JsonWriteFlags_None));
    check_eq_string(dynstring_view(&buffer), string_lit("false"));
  }

  it("can write null") {
    const JsonVal val = json_add_null(doc);

    json_write(&buffer, doc, val, &json_write_opts(.flags = JsonWriteFlags_None));
    check_eq_string(dynstring_view(&buffer), string_lit("null"));
  }

  it("can write nested values in pretty format") {
    const JsonVal obj = json_add_object(doc);
    json_add_field_str(doc, obj, string_lit("field"), json_add_number(doc, 42));

    const JsonVal arr = json_add_array(doc);
    json_add_elem(doc, arr, json_add_null(doc));
    json_add_elem(doc, arr, obj);
    json_add_elem(doc, arr, json_add_string(doc, string_lit("Hello")));

    const JsonVal obj2 = json_add_object(doc);
    json_add_field_str(doc, obj2, string_lit("a"), json_add_null(doc));
    json_add_field_str(doc, obj2, string_lit("b"), json_add_bool(doc, true));
    json_add_field_str(doc, obj2, string_lit("c"), json_add_string(doc, string_lit("Hello")));

    const JsonVal root = json_add_object(doc);
    json_add_field_str(doc, root, string_lit("arr"), arr);
    json_add_field_str(doc, root, string_lit("obj2"), obj2);

    json_write(&buffer, doc, root, &json_write_opts(.flags = JsonWriteFlags_Pretty));
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("{\n"
                   "  \"arr\": [\n"
                   "    null,\n"
                   "    {\n"
                   "      \"field\": 42\n"
                   "    },\n"
                   "    \"Hello\"\n"
                   "  ],\n"
                   "  \"obj2\": {\n"
                   "    \"a\": null,\n"
                   "    \"b\": true,\n"
                   "    \"c\": \"Hello\"\n"
                   "  }\n"
                   "}"));
  }

  teardown() {
    json_destroy(doc);
    dynstring_destroy(&buffer);
  }
}
