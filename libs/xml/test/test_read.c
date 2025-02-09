#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"
#include "core_dynstring.h"
#include "xml_doc.h"
#include "xml_eq.h"
#include "xml_read.h"

spec(read) {

  XmlDoc* doc = null;

  setup() { doc = xml_create(g_allocHeap, 0); }

  it("can read an element with a declaration") {
    XmlResult    res;
    const String rem = xml_read(
        doc,
        string_lit("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                   "<test/>"),
        &res);

    check(string_is_empty(rem));
    check_require(res.type == XmlResultType_Success);
    check_eq_int(xml_type(doc, res.node), XmlType_Element);
    check_eq_string(xml_name(doc, res.node), string_lit("test"));
    check_eq_string(xml_value(doc, res.node), string_empty);
  }

  it("can read an element without a declaration") {
    XmlResult    res;
    const String rem = xml_read(doc, string_lit("<test/>"), &res);

    check(string_is_empty(rem));
    check_require(res.type == XmlResultType_Success);
    check_eq_int(xml_type(doc, res.node), XmlType_Element);
    check_eq_string(xml_name(doc, res.node), string_lit("test"));
    check_eq_string(xml_value(doc, res.node), string_empty);
  }

  it("can read an element with text") {
    XmlResult    res;
    const String rem = xml_read(doc, string_lit("<test> Hello World! </test>"), &res);

    check(string_is_empty(rem));
    check_require(res.type == XmlResultType_Success);
    check_eq_int(xml_type(doc, res.node), XmlType_Element);
    check_eq_string(xml_name(doc, res.node), string_lit("test"));
    check_eq_string(xml_value(doc, res.node), string_empty);

    const XmlNode textNode = xml_first_child(doc, res.node);
    check_eq_int(xml_type(doc, textNode), XmlType_Text);
    check_eq_string(xml_value(doc, textNode), string_lit("Hello World!"));
  }

  it("can read an element with a comment") {
    XmlResult    res;
    const String rem = xml_read(doc, string_lit("<test><!-- Hello World! --></test>"), &res);

    check(string_is_empty(rem));
    check_require(res.type == XmlResultType_Success);
    check_eq_int(xml_type(doc, res.node), XmlType_Element);
    check_eq_string(xml_name(doc, res.node), string_lit("test"));
    check_eq_string(xml_value(doc, res.node), string_empty);

    const XmlNode textNode = xml_first_child(doc, res.node);
    check_eq_int(xml_type(doc, textNode), XmlType_Comment);
    check_eq_string(xml_value(doc, textNode), string_lit("Hello World!"));
  }

  it("can read complex elements") {
    // clang-format off
    XmlNode tmpNodeA, tmpNodeB, tmpNodeC, tmpNodeD, tmpNodeE;
    struct {
      String  input;
      XmlNode expected;
    } const data[] = {
        {string_lit("<a/>"), xml_add_elem(doc, sentinel_u32, string_lit("a"))},
        {string_lit("<a></a>"), xml_add_elem(doc, sentinel_u32, string_lit("a"))},
        {string_lit("<a>Hello</a>"), (
          tmpNodeA = xml_add_elem(doc, sentinel_u32, string_lit("a")),
            xml_add_text(doc, tmpNodeA, string_lit("Hello")),
          tmpNodeA
        ) },
        {string_lit("<a>Hello<b/></a>"), (
          tmpNodeB = xml_add_elem(doc, sentinel_u32, string_lit("a")),
            xml_add_text(doc, tmpNodeB, string_lit("Hello")),
            xml_add_elem(doc, tmpNodeB, string_lit("b")),
          tmpNodeB
        )},
        {string_lit("<a>Hello<b/>World</a>"), (
          tmpNodeC = xml_add_elem(doc, sentinel_u32, string_lit("a")),
            xml_add_text(doc, tmpNodeC, string_lit("Hello")),
            xml_add_elem(doc, tmpNodeC, string_lit("b")),
            xml_add_text(doc, tmpNodeC, string_lit("World")),
          tmpNodeC
        )},
        {string_lit("<a><b/>Hello<c/>World<d/></a>"), (
          tmpNodeD = xml_add_elem(doc, sentinel_u32, string_lit("a")),
            xml_add_elem(doc, tmpNodeD, string_lit("b")),
            xml_add_text(doc, tmpNodeD, string_lit("Hello")),
            xml_add_elem(doc, tmpNodeD, string_lit("c")),
            xml_add_text(doc, tmpNodeD, string_lit("World")),
            xml_add_elem(doc, tmpNodeD, string_lit("d")),
          tmpNodeD
        )},
        {string_lit("<a>Hello<!-- Foo -->World</a>"), (
          tmpNodeE = xml_add_elem(doc, sentinel_u32, string_lit("a")),
            xml_add_text(doc, tmpNodeE, string_lit("Hello")),
            xml_add_comment(doc, tmpNodeE, string_lit("Foo")),
            xml_add_text(doc, tmpNodeE, string_lit("World")),
          tmpNodeE
        )},
    };
    // clang-format on

    XmlResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      const String rem = xml_read(doc, data[i].input, &res);

      check(string_is_empty(rem));
      check_require(res.type == XmlResultType_Success);
      check(xml_eq(doc, res.node, data[i].expected));
    }
  }

  it("can read sequences of multiple nodes") {
    String input = string_lit("<a/><b>Hello</b><c>World</c><d/>");

    DynArray nodes = dynarray_create_over_t(mem_stack(256), XmlNode);

    while (!string_is_empty(input)) {
      XmlResult res;
      input = xml_read(doc, input, &res);
      check_require(res.type == XmlResultType_Success);
      *dynarray_push_t(&nodes, XmlNode) = res.node;
    }

    dynarray_destroy(&nodes);

    check_eq_int(xml_type(doc, *dynarray_at_t(&nodes, 0, XmlNode)), XmlType_Element);
    check_eq_int(xml_type(doc, *dynarray_at_t(&nodes, 1, XmlNode)), XmlType_Element);
    check_eq_int(xml_type(doc, *dynarray_at_t(&nodes, 2, XmlNode)), XmlType_Element);
    check_eq_int(xml_type(doc, *dynarray_at_t(&nodes, 3, XmlNode)), XmlType_Element);
  }

  it("resolves escapes in content") {
    struct {
      String input;
      String textResult;
    } const data[] = {
        {string_lit("Hello"), string_lit("Hello")},
        {string_lit("&lt;"), string_lit("<")},
        {string_lit("&gt;"), string_lit(">")},
        {string_lit("&amp;"), string_lit("&")},
        {string_lit("&apos;"), string_lit("'")},
        {string_lit("&quot;"), string_lit("\"")},
        {string_lit("&#9;"), string_lit("\t")},
        {string_lit("&#x9;"), string_lit("\t")},
        {string_lit("&#36;"), string_lit("$")},
        {string_lit("&#x24;"), string_lit("$")},
        {string_lit("&#x039B;"), string_lit("Λ")},
    };

    XmlResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      xml_read(doc, fmt_write_scratch("<a>{}</a>", fmt_text(data[i].input)), &res);
      check_require(res.type == XmlResultType_Success);

      const XmlNode textNode = xml_first_child(doc, res.node);
      check_eq_int(xml_type(doc, textNode), XmlType_Text);
      check_eq_string(xml_value(doc, textNode), data[i].textResult);
    }
  }

  it("fails on invalid input") {
    struct {
      String input;
      String error;
    } const data[] = {
        {string_empty, string_lit("Truncated")},
        {string_lit("<?"), string_lit("InvalidDeclStart")},
        {string_lit("<?0"), string_lit("InvalidDeclStart")},
        {string_lit("<"), string_lit("InvalidTagStart")},
        {string_lit("< "), string_lit("InvalidTagStart")},
        {string_lit("<0"), string_lit("InvalidTagStart")},
        {string_lit("</"), string_lit("InvalidTagEnd")},
        {string_lit("</0"), string_lit("InvalidTagEnd")},
        {string_lit("["), string_lit("InvalidChar")},
        {string_lit("\0"), string_lit("InvalidChar")},
        {string_lit("<a>\0"), string_lit("InvalidChar")},
        {string_lit("<!-- Hello"), string_lit("UnterminatedComment")},
        {string_lit("<!-- --"), string_lit("InvalidCommentTerminator")},
        {string_lit("<a>&foo;"), string_lit("InvalidReference")},
        {string_lit("<?foo?>"), string_lit("InvalidDecl")},
        {string_lit("<a foo=bar />"), string_lit("InvalidAttributeValue")},
        {string_lit("<a a=\"b\" a=\"c\" />"), string_lit("InvalidAttribute")},
        {string_lit("<a a=\"Hello"), string_lit("UnterminatedString")},
        {string_lit("<a a\"Hello\""), string_lit("UnexpectedToken")},
        {string_lit("<a></b>"), string_lit("MismatchedEndTag")},
    };

    XmlResult res;
    for (usize i = 0; i != array_elems(data); ++i) {
      xml_read(doc, data[i].input, &res);

      check_require(res.type == XmlResultType_Fail);
      check_eq_string(xml_error_str(res.error), data[i].error);
    }
  }

  it("fails when input contains too deep nesting") {
    DynString input   = dynstring_create_over(mem_stack(1024));
    const u32 nesting = 101;
    for (u32 i = 0; i != nesting; ++i) {
      dynstring_append(&input, string_lit("<a>"));
    }
    for (u32 i = 0; i != nesting; ++i) {
      dynstring_append(&input, string_lit("</a>"));
    }

    XmlResult res;
    xml_read(doc, dynstring_view(&input), &res);

    dynstring_destroy(&input);

    check_require(res.type == XmlResultType_Fail);
    check_eq_string(xml_error_str(res.error), string_lit("MaximumDepthExceeded"));
  }

  teardown() { xml_destroy(doc); }
}
