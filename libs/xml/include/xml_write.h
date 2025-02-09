#pragma once
#include "core_string.h"
#include "xml.h"

typedef enum {
  XmlWriteFlags_None            = 0,
  XmlWriteFlags_SkipDeclaration = 1 << 0,
} XmlWriteFlags;

/**
 * Formatting options for writing a Xml node.
 */
typedef struct {
  XmlWriteFlags flags;
  String        indent;
  String        newline;
} XmlWriteOpts;

/**
 * Formatting options for writing a Xml node.
 */
#define xml_write_opts(...)                                                                        \
  ((XmlWriteOpts){                                                                                 \
      .flags   = XmlWriteFlags_None,                                                               \
      .indent  = string_lit("  "),                                                                 \
      .newline = string_lit("\n"),                                                                 \
      __VA_ARGS__})

/**
 * Write a Xml node.
 * Supports a subset of Xml 1.0 (https://www.w3.org/TR/2008/REC-xml-20081126/).
 */
void xml_write(DynString*, const XmlDoc*, XmlNode, const XmlWriteOpts*);
