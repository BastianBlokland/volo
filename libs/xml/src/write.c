#include "core_diag.h"
#include "core_dynstring.h"
#include "xml_doc.h"
#include "xml_write.h"

typedef struct {
  const XmlDoc*       doc;
  const XmlWriteOpts* opts;
  DynString*          out;
  u32                 indent;
} XmlWriteState;

static void xml_write_node(XmlWriteState*, XmlNode);
static void xml_write_node_attr(XmlWriteState*, XmlNode);
static void xml_write_node_text(XmlWriteState*, XmlNode);

static void xml_write_separator(XmlWriteState* s) {
  dynstring_append(s->out, s->opts->newline);
  for (u32 i = 0; i != s->indent; ++i) {
    dynstring_append(s->out, s->opts->indent);
  }
}

static void xml_write_indent(XmlWriteState* s) {
  ++s->indent;
  xml_write_separator(s);
}

static void xml_write_outdent(XmlWriteState* s) {
  --s->indent;
  xml_write_separator(s);
}

static void xml_write_decl(XmlWriteState* s) {
  dynstring_append(s->out, string_lit("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
  xml_write_separator(s);
}

static void xml_write_node_elem(XmlWriteState* s, const XmlNode node) {
  dynstring_append_char(s->out, '<');
  dynstring_append(s->out, xml_name(s->doc, node));

  XmlNode attr = xml_first_attr(s->doc, node);
  for (; !sentinel_check(attr); attr = xml_next(s->doc, attr)) {
    dynstring_append_char(s->out, ' ');
    xml_write_node_attr(s, attr);
  }

  XmlNode child = xml_first_child(s->doc, node);
  if (sentinel_check(child)) {
    dynstring_append(s->out, string_lit("/>"));
    return;
  }

  dynstring_append_char(s->out, '>');

  const bool singleChild = sentinel_check(xml_next(s->doc, child));
  if (singleChild && xml_type(s->doc, child) == XmlType_Text) {
    xml_write_node_text(s, child);
  } else {
    xml_write_indent(s);
    for (; !sentinel_check(child); child = xml_next(s->doc, child)) {
      xml_write_separator(s);
      xml_write_node(s, child);
    }
    xml_write_outdent(s);
  }

  dynstring_append(s->out, string_lit("</"));
  dynstring_append(s->out, xml_name(s->doc, node));
  dynstring_append_char(s->out, '>');
}

static void xml_write_node_attr(XmlWriteState* s, const XmlNode node) {
  dynstring_append(s->out, xml_name(s->doc, node));
  dynstring_append_char(s->out, '=');
  dynstring_append_char(s->out, '\"');
  dynstring_append(s->out, xml_value(s->doc, node));
  dynstring_append_char(s->out, '\"');
}

static void xml_write_node_text(XmlWriteState* s, const XmlNode node) {
  // TODO: Escape content.
  dynstring_append(s->out, xml_value(s->doc, node));
}

static void xml_write_node_comment(XmlWriteState* s, const XmlNode node) {
  dynstring_append(s->out, string_lit("<!-- "));
  dynstring_append(s->out, xml_value(s->doc, node));
  dynstring_append(s->out, string_lit(" -->"));
}

static void xml_write_node(XmlWriteState* s, const XmlNode node) {
  switch (xml_type(s->doc, node)) {
  case XmlType_Element:
    xml_write_node_elem(s, node);
    return;
  case XmlType_Attribute:
    xml_write_node_attr(s, node);
    return;
  case XmlType_Text:
    xml_write_node_text(s, node);
    return;
  case XmlType_Comment:
    xml_write_node_comment(s, node);
    return;
  case XmlType_Count:
    break;
  }
  diag_crash();
}

void xml_write(DynString* str, const XmlDoc* doc, const XmlNode node, const XmlWriteOpts* opts) {
  XmlWriteState state = {
      .doc    = doc,
      .opts   = opts,
      .out    = str,
      .indent = 0,
  };
  if (!(opts->flags & XmlWriteFlags_SkipDeclaration)) {
    xml_write_decl(&state);
  }
  xml_write_node(&state, node);
}
