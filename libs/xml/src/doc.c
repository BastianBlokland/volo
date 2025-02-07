#include "core_alloc.h"
#include "core_dynarray.h"
#include "xml_doc.h"

typedef struct {
  u32 dummy;
} XmlNodeData;

struct sXmlDoc {
  Allocator* alloc;
  DynArray   nodes; // XmlNodeData[]
};

XmlDoc* xml_create(Allocator* alloc, const usize nodeCapacity) {
  XmlDoc* doc = alloc_alloc_t(alloc, XmlDoc);

  *doc = (XmlDoc){
      .alloc = alloc,
      .nodes = dynarray_create_t(alloc, XmlNodeData, nodeCapacity),
  };

  return doc;
}

void xml_destroy(XmlDoc* doc) {
  dynarray_destroy(&doc->nodes);
  alloc_free_t(doc->alloc, doc);
}
