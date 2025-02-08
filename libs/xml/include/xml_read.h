#pragma once
#include "xml.h"

typedef enum eXmlResultType {
  XmlResultType_Success,
  XmlResultType_Fail,
} XmlResultType;

typedef enum eXmlError {
  XmlError_InvalidDeclStart,
  XmlError_InvalidTagStart,
  XmlError_InvalidTagEnd,
  XmlError_InvalidChar,
  XmlError_InvalidCharInContent,
  XmlError_InvalidUtf8,
  XmlError_InvalidCommentTerminator,
  XmlError_InvalidReference,
  XmlError_InvalidDecl,
  XmlError_UnterminatedString,
  XmlError_UnterminatedComment,
  XmlError_ContentTooLong,

  XmlError_Count,
} XmlError;

/**
 * Result of parsing a Xml node.
 * If 'type == XmlResultType_Success' then 'node' contains a node in the provided XmlDoc.
 * else 'error' contains the reason why parsing failed.
 */
typedef struct sXmlResult {
  XmlResultType type;
  union {
    XmlNode  node;
    XmlError error;
  };
} XmlResult;

/**
 * Read a Xml node.
 * Supports a subset of Xml 1.0 (https://www.w3.org/TR/2008/REC-xml-20081126/).
 *
 * Returns the remaining input.
 * The result is written to the output pointer.
 *
 * Pre-condition: res != null.
 */
String xml_read(XmlDoc*, String, XmlResult* res);
