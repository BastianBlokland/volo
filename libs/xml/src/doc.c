#include "core_alloc.h"
#include "core_dynarray.h"
#include "xml_doc.h"

struct sXmlDoc {
  Allocator* alloc;
  DynArray   attributes; // XmlAttribute[]
};

XmlDoc* xml_create(Allocator* alloc) {
  XmlDoc* doc = alloc_alloc_t(alloc, XmlDoc);

  *doc = (XmlDoc){
      .alloc      = alloc,
      .attributes = dynarray_create_t(g_allocHeap, XmlAttribute, 16),
  };

  return doc;
}

void xml_destroy(XmlDoc* doc) {
  dynarray_destroy(&doc->attributes);
  alloc_free_t(doc->alloc, doc);
}
