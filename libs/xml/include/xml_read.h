#pragma once
#include "xml.h"

typedef enum eXmlError {
  XmlError_InvalidTagStart,
  XmlError_InvalidTagEnd,
  XmlError_InvalidChar,
  XmlError_InvalidUtf8,
  XmlError_InvalidCommentTerminator,
  XmlError_UnterminatedString,
  XmlError_UnterminatedComment,

  XmlError_Count,
} XmlError;
