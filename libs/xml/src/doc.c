#include "core_alloc.h"
#include "xml_doc.h"

struct sXmlDoc {
  Allocator* alloc;
};

XmlDoc* xml_create(Allocator* alloc) {
  XmlDoc* doc = alloc_alloc_t(alloc, XmlDoc);

  *doc = (XmlDoc){.alloc = alloc};

  return doc;
}

void xml_destroy(XmlDoc* doc) { alloc_free_t(doc->alloc, doc); }
