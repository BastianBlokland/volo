#pragma once
#include "core_string.h"
#include "xml.h"

/**
 * Definition for a Xml Document.
 * Supports a subset of Xml 1.0 (https://www.w3.org/TR/2008/REC-xml-20081126/).
 */
typedef struct sXmlDoc XmlDoc;

typedef struct sXmlAttribute {
  String key, value;
} XmlAttribute;

/**
 * Create a new Xml document.
 *
 * Should be destroyed using 'xml_destroy()'.
 */
XmlDoc* xml_create(Allocator*);

/**
 * Destroy a Xml document.
 */
void xml_destroy(XmlDoc*);
