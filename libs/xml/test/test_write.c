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

  it("can write a basic node") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    xml_write(&buffer, doc, node, &xml_write_opts());
    check_eq_string(
        dynstring_view(&buffer),
        string_lit("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<test/>"));
  }

  it("can write a basic node without a declaration") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(dynstring_view(&buffer), string_lit("<test/>"));
  }

  it("can write a basic node with an attribute") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_attr(doc, node, string_lit("foo"), string_lit("bar"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(dynstring_view(&buffer), string_lit("<test foo=\"bar\"/>"));
  }

  it("can write a basic node with multiple attributes") {
    const XmlNode node = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_attr(doc, node, string_lit("foo"), string_lit("bar"));
    xml_add_attr(doc, node, string_lit("hello"), string_lit("world"));
    xml_add_attr(doc, node, string_lit("test"), string_lit("world"));

    xml_write(&buffer, doc, node, &xml_write_opts(.flags = XmlWriteFlags_SkipDeclaration));
    check_eq_string(
        dynstring_view(&buffer), string_lit("<test foo=\"bar\" hello=\"world\" test=\"world\"/>"));
  }

  teardown() {
    xml_destroy(doc);
    dynstring_destroy(&buffer);
  }
}
