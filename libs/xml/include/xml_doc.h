#pragma once
#include "core_string.h"
#include "xml.h"

/**
 * Definition for a Xml Document.
 * Supports a subset of Xml 1.0 (https://www.w3.org/TR/2008/REC-xml-20081126/).
 */
typedef struct sXmlDoc XmlDoc;

typedef enum eXmlType {
  XmlType_Attribute,
  XmlType_Element,
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
 * TODO:
 */
XmlNode xml_add_attr(XmlDoc*, XmlNode parent, String name, String value);

/**
 * TODO:
 */
XmlNode xml_add_elem(XmlDoc*, XmlNode parent, String name);

/**
 * TODO:
 */
XmlNode xml_add_text(XmlDoc*, XmlNode parent, String text);
