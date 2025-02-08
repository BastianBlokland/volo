#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "xml_doc.h"

spec(doc) {

  XmlDoc* doc = null;

  setup() { doc = xml_create(g_allocHeap, 0); }

  it("can store an element") {
    const XmlNode elem = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    check_eq_int(xml_type(doc, elem), XmlType_Element);
    check_eq_string(xml_name(doc, elem), string_lit("test"));
  }

  it("can add an attribute to an element") {
    const XmlNode elem = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    check(sentinel_check(xml_first_attr(doc, elem)));

    const XmlNode attr = xml_add_attr(doc, elem, string_lit("a"), string_lit("valA"));
    check_eq_int(xml_type(doc, attr), XmlType_Attribute);
    check_eq_string(xml_name(doc, attr), string_lit("a"));
    check_eq_string(xml_value(doc, attr), string_lit("valA"));

    check(xml_first_attr(doc, elem) == attr);

    check(xml_attr_has(doc, elem, string_lit("a")));
    check(!xml_attr_has(doc, elem, string_lit("b")));
    check_eq_string(xml_attr_get(doc, elem, string_lit("a")), string_lit("valA"));
  }

  it("can add multiple attributes to an element") {
    const XmlNode elem = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    static const struct {
      String name, value;
    } g_testAttrs[] = {
        {string_static("a"), string_static("valA")},
        {string_static("b"), string_static("valB")},
        {string_static("c"), string_static("valC")},
        {string_static("d"), string_static("valD")},
    };

    for (u32 i = 0; i != array_elems(g_testAttrs); ++i) {
      const XmlNode attr = xml_add_attr(doc, elem, g_testAttrs[i].name, g_testAttrs[i].value);
      check_eq_int(xml_type(doc, attr), XmlType_Attribute);
      check_eq_string(xml_name(doc, attr), g_testAttrs[i].name);
      check_eq_string(xml_value(doc, attr), g_testAttrs[i].value);

      check(xml_attr_has(doc, elem, g_testAttrs[i].name));
      check_eq_string(xml_attr_get(doc, elem, g_testAttrs[i].name), g_testAttrs[i].value);
    }

    for (u32 i = 0; i != array_elems(g_testAttrs); ++i) {
      check(xml_attr_has(doc, elem, g_testAttrs[i].name));
      check_eq_string(xml_attr_get(doc, elem, g_testAttrs[i].name), g_testAttrs[i].value);
    }
  }

  it("fails to add an attribute with a duplicate name") {
    const XmlNode elem = xml_add_elem(doc, sentinel_u32, string_lit("test"));

    xml_add_attr(doc, elem, string_lit("a"), string_lit("valA"));

    check(sentinel_check(xml_add_attr(doc, elem, string_lit("a"), string_lit("valB"))));
    check_eq_string(xml_attr_get(doc, elem, string_lit("a")), string_lit("valA"));
  }

  it("can add a child element to an element") {
    const XmlNode parent = xml_add_elem(doc, sentinel_u32, string_lit("parent"));

    check(sentinel_check(xml_first_child(doc, parent)));

    const XmlNode child = xml_add_elem(doc, parent, string_lit("child"));
    check_eq_int(xml_type(doc, child), XmlType_Element);
    check_eq_string(xml_name(doc, child), string_lit("child"));

    check(xml_first_child(doc, parent) == child);
  }

  it("can add a text node to an element") {
    const XmlNode parent = xml_add_elem(doc, sentinel_u32, string_lit("parent"));

    check(sentinel_check(xml_first_child(doc, parent)));

    const XmlNode child = xml_add_text(doc, parent, string_lit("Hello World!"));
    check_eq_int(xml_type(doc, child), XmlType_Text);
    check_eq_string(xml_value(doc, child), string_lit("Hello World!"));

    check(xml_first_child(doc, parent) == child);
  }

  it("can add a comment node to an element") {
    const XmlNode parent = xml_add_elem(doc, sentinel_u32, string_lit("parent"));

    check(sentinel_check(xml_first_child(doc, parent)));

    const XmlNode child = xml_add_comment(doc, parent, string_lit("Hello World!"));
    check_eq_int(xml_type(doc, child), XmlType_Comment);
    check_eq_string(xml_value(doc, child), string_lit("Hello World!"));

    check(xml_first_child(doc, parent) == child);
  }

  it("can add a multiple children to an element") {
    const XmlNode parent = xml_add_elem(doc, sentinel_u32, string_lit("parent"));

    check(sentinel_check(xml_first_child(doc, parent)));

    const XmlNode c1 = xml_add_elem(doc, parent, string_lit("child1"));
    const XmlNode c2 = xml_add_elem(doc, parent, string_lit("child2"));
    const XmlNode c3 = xml_add_text(doc, parent, string_lit("Hello World!"));
    const XmlNode c4 = xml_add_comment(doc, parent, string_lit("Hello World!"));
    const XmlNode c5 = xml_add_elem(doc, parent, string_lit("child3"));

    XmlNode itr;
    check_eq_int((itr = xml_first_child(doc, parent)), c1);
    check_eq_int((itr = xml_next(doc, itr)), c2);
    check_eq_int((itr = xml_next(doc, itr)), c3);
    check_eq_int((itr = xml_next(doc, itr)), c4);
    check_eq_int((itr = xml_next(doc, itr)), c5);
    check(sentinel_check(xml_next(doc, itr)));
  }

  teardown() { xml_destroy(doc); }
}
