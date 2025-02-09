#include "check_spec.h"
#include "core_alloc.h"
#include "xml_doc.h"
#include "xml_eq.h"

spec(eq) {

  XmlDoc* doc = null;

  setup() { doc = xml_create(g_allocHeap, 0); }

  it("returns true if both nodes are a sentinel") {
    check(xml_eq(doc, sentinel_u32, sentinel_u32));
  }

  it("returns false if one node is a sentinel") {
    check(!xml_eq(doc, sentinel_u32, xml_add_elem(doc, sentinel_u32, string_lit("test"))));
    check(!xml_eq(doc, xml_add_elem(doc, sentinel_u32, string_lit("test")), sentinel_u32));
  }

  it("can equate text") {
    const XmlNode root = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    const XmlNode a = xml_add_text(doc, root, string_lit("Hello"));
    const XmlNode b = xml_add_text(doc, root, string_lit("World"));
    const XmlNode c = xml_add_text(doc, root, string_lit("Hello"));

    check(xml_eq(doc, a, c));
    check(!xml_eq(doc, a, b));
    check(!xml_eq(doc, b, c));
  }

  it("can equate comments") {
    const XmlNode root = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    const XmlNode a = xml_add_comment(doc, root, string_lit("Hello"));
    const XmlNode b = xml_add_comment(doc, root, string_lit("World"));
    const XmlNode c = xml_add_comment(doc, root, string_lit("Hello"));

    check(xml_eq(doc, a, c));
    check(!xml_eq(doc, a, b));
    check(!xml_eq(doc, b, c));
  }

  it("can equate attributes") {
    const XmlNode rootA = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    const XmlNode rootB = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    const XmlNode attrA = xml_add_attr(doc, rootA, string_lit("t1"), string_lit("Hello"));
    const XmlNode attrB = xml_add_attr(doc, rootB, string_lit("t2"), string_lit("World"));
    const XmlNode attrC = xml_add_attr(doc, rootB, string_lit("t1"), string_lit("Hello"));
    const XmlNode attrD = xml_add_attr(doc, rootA, string_lit("t2"), string_lit("Hello"));

    check(xml_eq(doc, attrA, attrC));
    check(!xml_eq(doc, attrA, attrB));
    check(!xml_eq(doc, attrB, attrC));
    check(!xml_eq(doc, attrB, attrD));
  }

  it("can equate elements") {
    const XmlNode rootA = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_attr(doc, rootA, string_lit("test"), string_lit("Hello"));
    xml_add_text(doc, rootA, string_lit("Some text"));
    xml_add_comment(doc, rootA, string_lit("Some comment"));

    const XmlNode rootB = xml_add_elem(doc, sentinel_u32, string_lit("test"));
    xml_add_attr(doc, rootB, string_lit("test"), string_lit("World"));
    xml_add_text(doc, rootB, string_lit("Some text"));
    xml_add_comment(doc, rootB, string_lit("Some comment"));

    check(xml_eq(doc, rootA, rootA));
    check(xml_eq(doc, rootB, rootB));
    check(!xml_eq(doc, rootA, rootB));
  }

  teardown() { xml_destroy(doc); }
}
