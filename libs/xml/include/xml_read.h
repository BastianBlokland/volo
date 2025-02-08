#pragma once
#include "xml.h"

typedef enum eXmlError {
  XmlError_InvalidTagStart,
  XmlError_InvalidTagEnd,
  XmlError_InvalidChar,

  XmlError_Count,
} XmlError;
