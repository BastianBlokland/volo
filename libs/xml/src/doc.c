#include "core_alloc.h"
#include "core_dynarray.h"
#include "xml_doc.h"

#define xml_str_chunk_size (16 * usize_kibibyte)

typedef struct {
  u32 dummy;
} XmlNodeData;

struct sXmlDoc {
  Allocator* alloc;
  Allocator* allocStr; // (chunked) bump allocator for string data.
  DynArray   nodes;    // XmlNodeData[]
};

XmlDoc* xml_create(Allocator* alloc, const usize nodeCapacity) {
  XmlDoc* doc = alloc_alloc_t(alloc, XmlDoc);

  *doc = (XmlDoc){
      .alloc    = alloc,
      .allocStr = alloc_chunked_create(alloc, alloc_bump_create, xml_str_chunk_size),
      .nodes    = dynarray_create_t(alloc, XmlNodeData, nodeCapacity),
  };

  return doc;
}

void xml_destroy(XmlDoc* doc) {
  dynarray_destroy(&doc->nodes);
  alloc_chunked_destroy(doc->allocStr); // Free all string data.
  alloc_free_t(doc->alloc, doc);
}
