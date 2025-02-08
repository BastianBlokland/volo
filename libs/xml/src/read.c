#include "core_string.h"
#include "xml_doc.h"
#include "xml_read.h"

#include "lex_internal.h"

typedef struct {
  XmlDoc* doc;
} XmlReadState;

#define xml_err(_ERR_)                                                                             \
  (XmlResult) { .type = XmlResultType_Fail, .error = (_ERR_) }

#define xml_success(_VAL_)                                                                         \
  (XmlResult) { .type = XmlResultType_Success, .val = (_VAL_) }

static String xml_read_decl(XmlReadState* state, String input, XmlResult* res) {
  (void)state;
  XmlToken token;
  input = xml_lex(input, XmlPhase_Markup, &token);
  if (token.type == XmlTokenType_Error) {
    return *res = xml_err(token.val_error), input;
  }
  if (token.type != XmlTokenType_DeclStart || !string_eq(token.val_decl, string_lit("xml"))) {
    goto InvalidDecl;
  }

  for (;;) {
    input = xml_lex(input, XmlPhase_Markup, &token);
    switch (token.type) {
    case XmlTokenType_Error:
      return *res = xml_err(token.val_error), input;
    case XmlTokenType_DeclClose:
      return input;
    case XmlTokenType_Name:
      break; // Attribute start.
    default:
      goto InvalidDecl;
    }
    input = xml_lex(input, XmlPhase_Markup, &token);
    if (token.type != XmlTokenType_Equal) {
      goto InvalidDecl;
    }
    input = xml_lex(input, XmlPhase_Markup, &token);
    if (token.type != XmlTokenType_String) {
      goto InvalidDecl;
    }
    continue;
  }

InvalidDecl:
  return *res = xml_err(XmlError_InvalidDecl), input;
}

String xml_read(XmlDoc* doc, String input, XmlResult* res) {
  XmlReadState state = {
      .doc = doc,
  };
  input = xml_read_decl(&state, input, res);
  return input;
}
