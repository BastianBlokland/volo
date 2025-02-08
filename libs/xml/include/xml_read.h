#pragma once
#include "xml.h"

typedef enum eXmlError {
  XmlError_InvalidStartTag,
  XmlError_InvalidEndTag,
  XmlError_InvalidChar,

  XmlError_Count,
} XmlError;
