#pragma once
#include "core_string.h"
#include "xml.h"

/**
 * Definition for a Xml Document.
 * Supports a subset of Xml 1.0 (https://www.w3.org/TR/2008/REC-xml-20081126/).
 */
typedef struct sXmlDoc XmlDoc;

typedef enum eXmlType {
  XmlType_Element,
  XmlType_Attribute,
  XmlType_Text,
  XmlType_Comment,

  XmlType_Count,
} XmlType;

/**
 * Handle to a Xml node.
 * 'sentinel_u32' used as a sentinel.
 */
typedef u32 XmlNode;

/**
 * Create a new Xml document.
 * NOTE: 'nodeCapacity' is only the initial capacity, more space is automatically allocated when
 * required. Capacity of 0 is legal and will allocate memory when the first node is added.
 *
 * Should be destroyed using 'xml_destroy()'.
 */
XmlDoc* xml_create(Allocator*, usize nodeCapacity);

/**
 * Destroy a Xml document.
 */
void xml_destroy(XmlDoc*);

/**
 * Clear a Xml document.
 * NOTE: After clearing all previously added Xml nodes are invalided.
 */
void xml_clear(XmlDoc*);

/**
 * Add a new element node to the document.
 * Optionally provide a parent element node, provide 'sentinel_u32' to make a root element.
 */
XmlNode xml_add_elem(XmlDoc*, XmlNode parent, String name);

/**
 * Add a new attribute node to an element node.
 * Returns 'sentinel_u32' when the parent element already had an attribute with the same name.
 */
XmlNode xml_add_attr(XmlDoc*, XmlNode parent, String name, String value);

/**
 * Add a new text node to an element node.
 */
XmlNode xml_add_text(XmlDoc*, XmlNode parent, String value);

/**
 * Add a new comment node to an element node.
 */
XmlNode xml_add_comment(XmlDoc*, XmlNode parent, String value);

/**
 * Query node data.
 */
XmlType xml_type(const XmlDoc*, XmlNode);
String  xml_name(const XmlDoc*, XmlNode);
String  xml_value(const XmlDoc*, XmlNode);
bool    xml_attr_has(const XmlDoc*, XmlNode node, String name);
String  xml_attr_get(const XmlDoc*, XmlNode node, String name);
XmlNode xml_first_child(const XmlDoc*, XmlNode);
XmlNode xml_first_attr(const XmlDoc*, XmlNode);

/**
 * Retrieve the next (sibling) node.
 * Returns 'sentinel_u32' when there are no more sibling nodes.
 */
XmlNode xml_next(const XmlDoc*, XmlNode node);
