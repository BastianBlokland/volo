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

  teardown() {
    xml_destroy(doc);
    dynstring_destroy(&buffer);
  }
}
