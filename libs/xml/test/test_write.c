#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "xml_doc.h"
#include "xml_write.h"

spec(write) {

  XmlDoc*   doc    = null;
  DynString buffer = {0};

  setup() {
    doc    = xml_create(g_allocHeap, 0);
    buffer = dynstring_create(g_allocPage, usize_kibibyte * 4);
  }

  it("can write a node") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    xml_write(&buffer, doc, node, &xml_write_opts());
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<test/>"));
  }

  it("can write a node without a declaration") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(dynstring_view(&buffer), string_lit("<test/>"));
  }

  it("can write a node with an attribute") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_attr(doc, node, string_lit("foo"), string_lit("bar"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(dynstring_view(&buffer), string_lit("<test foo=\"bar\"/>"));
  }

  it("can write a node with multiple attributes") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_attr(doc, node, string_lit("foo"), string_lit("bar"));
    xml_add_attr(doc, node, string_lit("hello"), string_lit("world"));
    xml_add_attr(doc, node, string_lit("test"), string_lit("world"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(
        dynstring_view(&buffer), string_lit("<test foo=\"bar\" hello=\"world\" test=\"world\"/>"));
  }

  it("can write a node with text") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_text(doc, node, string_lit("Hello World!"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(dynstring_view(&buffer), string_lit("<test>Hello World!</test>"));
  }

  it("can write a node with a child node") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_elem(doc, node, string_lit("foo"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("<test>\n"
                   "  <foo/>\n"
                   "</test>"));
  }

  it("can write a node with multiple child nodes") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_elem(doc, node, string_lit("foo"));
    xml_add_elem(doc, node, string_lit("bar"));
    xml_add_elem(doc, node, string_lit("baz"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("<test>\n"
                   "  <foo/>\n"
                   "  <bar/>\n"
                   "  <baz/>\n"
                   "</test>"));
  }

  it("can write a node with mixed child nodes") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_text(doc, node, string_lit("Hello"));
    xml_add_comment(doc, node, string_lit("Test!"));
    xml_add_elem(doc, node, string_lit("bar"));
    xml_add_text(doc, node, string_lit("World"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("<test>\n"
                   "  Hello\n"
                   "  <!-- Test! -->\n"
                   "  <bar/>\n"
                   "  World\n"
                   "</test>"));
  }

  it("can write nested nodes") {
    const XmlNode node       = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    const XmlNode child      = xml_add_elem(doc, node, string_lit("foo"));
    const XmlNode grandChild = xml_add_elem(doc, child, string_lit("bar"));
    xml_add_elem(doc, grandChild, string_lit("baz"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("<test>\n"
                   "  <foo>\n"
                   "    <bar>\n"
                   "      <baz/>\n"
                   "    </bar>\n"
                   "  </foo>\n"
                   "</test>"));
  }

  teardown() {
    xml_destroy(doc);
    dynstring_destroy(&buffer);
  }
}
