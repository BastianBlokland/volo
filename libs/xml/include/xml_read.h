#pragma once
#include "xml.h"

typedef enum eXmlError {
  XmlError_InvalidTagStart,
  XmlError_InvalidTagEnd,
  XmlError_InvalidChar,
  XmlError_InvalidUtf8,
  XmlError_UnterminatedString,

  XmlError_Count,
} XmlError;
