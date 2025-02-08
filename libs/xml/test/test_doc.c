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

    const XmlNode attr = xml_add_attr(doc, elem, string_lit("a"), string_lit("valA"));
    check_eq_int(xml_type(doc, attr), XmlType_Attribute);
    check_eq_string(xml_name(doc, attr), string_lit("a"));
    check_eq_string(xml_value(doc, attr), string_lit("valA"));

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

  teardown() { xml_destroy(doc); }
}
