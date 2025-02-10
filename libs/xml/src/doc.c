#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_sentinel.h"
#include "core_stringtable.h"
#include "xml_doc.h"

#define xml_str_chunk_size (32 * usize_kibibyte)

typedef struct {
  StringHash nameHash;
  XmlNode    attrHead;
  XmlNode    childHead, childTail;
} XmlElemData;

typedef struct {
  StringHash nameHash;
  String     value;
} XmlAttrData;

typedef struct {
  String value;
} XmlTextData;

typedef struct {
  String value;
} XmlCommentData;

typedef struct {
  XmlType type;
  XmlNode next;
  union {
    XmlElemData    data_elem;
    XmlAttrData    data_attr;
    XmlTextData    data_text;
    XmlCommentData data_comment;
  };
} XmlNodeData;

struct sXmlDoc {
  Allocator*   alloc;
  Allocator*   allocStr; // (chunked) bump allocator for string data.
  StringTable* keyTable; // Table for storing string keys (expected to contain many duplicates).
  DynArray     nodes;    // XmlNodeData[]
};

static String xml_string_store(XmlDoc* doc, const String str) {
  if (string_is_empty(str)) {
    return string_empty;
  }
  const String storedStr = string_dup(doc->allocStr, str);
  if (UNLIKELY(!mem_valid(storedStr))) {
    diag_crash_msg("Xml doc string allocator ran out of space");
  }
  return storedStr;
}

INLINE_HINT static XmlNodeData* xml_node_data(const XmlDoc* doc, const XmlNode node) {
  diag_assert_msg(node < doc->nodes.size, "Out of bounds XmlNode");
  return &dynarray_begin_t(&doc->nodes, XmlNodeData)[node];
}

static XmlNode xml_node_add(XmlDoc* doc, const XmlNodeData data) {
  const XmlNode node                         = (XmlNode)doc->nodes.size;
  *dynarray_push_t(&doc->nodes, XmlNodeData) = data;
  return node;
}

static void xml_node_link_child(XmlDoc* doc, const XmlNode elem, const XmlNode child) {
  diag_assert_msg(xml_type(doc, elem) == XmlType_Element, "Invalid element value");
  diag_assert_msg(elem != child, "Xml cannot contain cycles"); // TODO: Detect indirect cycles.

  XmlNodeData* elemData = xml_node_data(doc, elem);

  // Add the child to the end of the element's children linked-list.
  if (sentinel_check(elemData->data_elem.childTail)) {
    elemData->data_elem.childHead = child;
    elemData->data_elem.childTail = child;
  } else {
    xml_node_data(doc, elemData->data_elem.childTail)->next = child;
    elemData->data_elem.childTail                           = child;
  }
}

static bool xml_node_link_attr(XmlDoc* doc, const XmlNode elem, const XmlNode attr) {
  diag_assert_msg(xml_type(doc, elem) == XmlType_Element, "Invalid element value");
  diag_assert_msg(xml_type(doc, attr) == XmlType_Attribute, "Invalid attribute value");

  XmlNodeData*       elemData = xml_node_data(doc, elem);
  const XmlNodeData* attrData = xml_node_data(doc, attr);

  // Walk the linked-list of attributes to check for duplicate names and to find the last link.
  XmlNode* link = &elemData->data_elem.attrHead;
  while (!sentinel_check(*link)) {
    XmlNodeData* linkAttrData = xml_node_data(doc, *link);
    if (attrData->data_attr.nameHash == linkAttrData->data_attr.nameHash) {
      return false; // Existing attribute found with the same name.
    }
    link = &linkAttrData->next;
  }

  *link = attr;
  return true; // Attribute added.
}

XmlDoc* xml_create(Allocator* alloc, const usize nodeCapacity) {
  XmlDoc* doc = alloc_alloc_t(alloc, XmlDoc);

  *doc = (XmlDoc){
      .alloc    = alloc,
      .allocStr = alloc_chunked_create(alloc, alloc_bump_create, xml_str_chunk_size),
      .keyTable = stringtable_create(alloc),
      .nodes    = dynarray_create_t(alloc, XmlNodeData, nodeCapacity),
  };

  return doc;
}

void xml_destroy(XmlDoc* doc) {
  dynarray_destroy(&doc->nodes);
  alloc_chunked_destroy(doc->allocStr); // Free all string data.
  stringtable_destroy(doc->keyTable);
  alloc_free_t(doc->alloc, doc);
}

void xml_clear(XmlDoc* doc) {
  alloc_reset(doc->allocStr); // Clear all string data.
  stringtable_reset(doc->keyTable);
  dynarray_clear(&doc->nodes);
}

XmlNode xml_add_elem(XmlDoc* doc, const XmlNode parent, const String name) {
  if (!sentinel_check(parent) && xml_type(doc, parent) != XmlType_Element) {
    return sentinel_u32;
  }

  const XmlNode node = xml_node_add(
      doc,
      (XmlNodeData){
          .type = XmlType_Element,
          .next = sentinel_u32,
          .data_elem =
              {
                  .nameHash  = stringtable_add(doc->keyTable, name),
                  .attrHead  = sentinel_u32,
                  .childHead = sentinel_u32,
                  .childTail = sentinel_u32,
              },
      });

  if (!sentinel_check(parent)) {
    xml_node_link_child(doc, parent, node);
  }
  return node;
}

XmlNode xml_add_attr(XmlDoc* doc, const XmlNode parent, const String name, const String value) {
  if (sentinel_check(parent) || xml_type(doc, parent) != XmlType_Element) {
    return sentinel_u32;
  }

  const XmlNode node = xml_node_add(
      doc,
      (XmlNodeData){
          .type = XmlType_Attribute,
          .next = sentinel_u32,
          .data_attr =
              {
                  .nameHash = stringtable_add(doc->keyTable, name),
                  .value    = xml_string_store(doc, value),
              },
      });

  if (xml_node_link_attr(doc, parent, node)) {
    return node;
  }
  // TODO: The added attribute node is left as unreachable in the document.
  return sentinel_u32;
}

XmlNode xml_add_text(XmlDoc* doc, const XmlNode parent, const String value) {
  if (sentinel_check(parent) || xml_type(doc, parent) != XmlType_Element) {
    return sentinel_u32;
  }

  const XmlNode node = xml_node_add(
      doc,
      (XmlNodeData){
          .type      = XmlType_Text,
          .next      = sentinel_u32,
          .data_text = {.value = xml_string_store(doc, value)},
      });

  xml_node_link_child(doc, parent, node);
  return node;
}

XmlNode xml_add_comment(XmlDoc* doc, const XmlNode parent, const String value) {
  if (sentinel_check(parent) || xml_type(doc, parent) != XmlType_Element) {
    return sentinel_u32;
  }

  const XmlNode node = xml_node_add(
      doc,
      (XmlNodeData){
          .type      = XmlType_Comment,
          .next      = sentinel_u32,
          .data_text = {.value = xml_string_store(doc, value)},
      });

  xml_node_link_child(doc, parent, node);
  return node;
}

bool xml_is(const XmlDoc* doc, const XmlNode node, const XmlType type) {
  return !sentinel_check(node) && xml_node_data(doc, node)->type == type;
}

XmlType xml_type(const XmlDoc* doc, const XmlNode node) { return xml_node_data(doc, node)->type; }

String xml_name(const XmlDoc* doc, const XmlNode node) {
  XmlNodeData* nodeData = xml_node_data(doc, node);
  switch (nodeData->type) {
  case XmlType_Element:
    return stringtable_lookup(doc->keyTable, nodeData->data_elem.nameHash);
  case XmlType_Attribute:
    return stringtable_lookup(doc->keyTable, nodeData->data_attr.nameHash);
  default:
    return string_empty;
  }
}

String xml_value(const XmlDoc* doc, const XmlNode node) {
  XmlNodeData* nodeData = xml_node_data(doc, node);
  switch (nodeData->type) {
  case XmlType_Attribute:
    return nodeData->data_attr.value;
  case XmlType_Text:
    return nodeData->data_text.value;
  case XmlType_Comment:
    return nodeData->data_comment.value;
  default:
    return string_empty;
  }
}

bool xml_attr_has(const XmlDoc* doc, const XmlNode node, const StringHash nameHash) {
  XmlNodeData* nodeData = xml_node_data(doc, node);
  if (nodeData->type != XmlType_Element) {
    return false;
  }

  // Walk the linked-list of attributes.
  const XmlNode attrHead = nodeData->data_elem.attrHead;
  for (XmlNode attr = attrHead; !sentinel_check(attr); attr = xml_node_data(doc, attr)->next) {
    if (xml_node_data(doc, attr)->data_attr.nameHash == nameHash) {
      return true;
    }
  }

  return false;
}

String xml_attr_get(const XmlDoc* doc, const XmlNode node, const StringHash nameHash) {
  XmlNodeData* nodeData = xml_node_data(doc, node);
  if (nodeData->type != XmlType_Element) {
    return string_empty;
  }

  // Walk the linked-list of attributes.
  const XmlNode attrHead = nodeData->data_elem.attrHead;
  for (XmlNode attr = attrHead; !sentinel_check(attr); attr = xml_node_data(doc, attr)->next) {
    const XmlNodeData* attrData = xml_node_data(doc, attr);
    if (attrData->data_attr.nameHash == nameHash) {
      return attrData->data_attr.value;
    }
  }

  return string_empty;
}

XmlNode xml_first_child(const XmlDoc* doc, const XmlNode node) {
  XmlNodeData* nodeData = xml_node_data(doc, node);
  if (nodeData->type != XmlType_Element) {
    return sentinel_u32;
  }
  return nodeData->data_elem.childHead;
}

XmlNode xml_first_attr(const XmlDoc* doc, const XmlNode node) {
  XmlNodeData* nodeData = xml_node_data(doc, node);
  if (nodeData->type != XmlType_Element) {
    return sentinel_u32;
  }
  return nodeData->data_elem.attrHead;
}

XmlNode xml_next(const XmlDoc* doc, const XmlNode node) {
  if (sentinel_check(node)) {
    return sentinel_u32;
  }
  return xml_node_data(doc, node)->next;
}
