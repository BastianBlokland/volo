#pragma once
#include "xml.h"

typedef enum eXmlError {
  XmlError_InvalidDeclStart,
  XmlError_InvalidTagStart,
  XmlError_InvalidTagEnd,
  XmlError_InvalidChar,
  XmlError_InvalidCharInContent,
  XmlError_InvalidUtf8,
  XmlError_InvalidCommentTerminator,
  XmlError_InvalidReference,
  XmlError_UnterminatedString,
  XmlError_UnterminatedComment,
  XmlError_ContentTooLong,

  XmlError_Count,
} XmlError;
