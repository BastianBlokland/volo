#include "core_string.h"
#include "xml_doc.h"
#include "xml_read.h"

#include "lex_internal.h"

typedef struct {
  XmlDoc* doc;
  String  input;
} XmlReadContext;

#define xml_err(_ERR_)                                                                             \
  (XmlResult) { .type = XmlResultType_Fail, .error = (_ERR_) }

#define xml_success(_VAL_)                                                                         \
  (XmlResult) { .type = XmlResultType_Success, .val = (_VAL_) }

static XmlToken read_peek(XmlReadContext* ctx, const XmlPhase phase) {
  XmlToken token;
  xml_lex(ctx->input, phase, &token);
  return token;
}

static XmlToken read_consume(XmlReadContext* ctx, const XmlPhase phase) {
  XmlToken token;
  ctx->input = xml_lex(ctx->input, phase, &token);
  return token;
}

static void read_decl(XmlReadContext* ctx, XmlResult* res) {
  XmlToken token = read_consume(ctx, XmlPhase_Markup);
  if (token.type != XmlTokenType_DeclStart || !string_eq(token.val_decl, string_lit("xml"))) {
    *res = xml_err(XmlError_InvalidDecl);
    return;
  }
  for (;;) {
    token = read_consume(ctx, XmlPhase_Markup);
    switch (token.type) {
    case XmlTokenType_Error:
      *res = xml_err(token.val_error);
      return;
    case XmlTokenType_DeclClose:
      return;
    case XmlTokenType_Name:
      break; // Attribute start.
    default:
      *res = xml_err(XmlError_InvalidDecl);
      return;
    }
    token = read_consume(ctx, XmlPhase_Markup);
    if (token.type != XmlTokenType_Equal) {
      *res = xml_err(XmlError_InvalidDecl);
      return;
    }
    token = read_consume(ctx, XmlPhase_Markup);
    if (token.type != XmlTokenType_String) {
      *res = xml_err(XmlError_InvalidDecl);
      return;
    }
  }
}

String xml_read(XmlDoc* doc, const String input, XmlResult* res) {
  XmlReadContext ctx = {
      .doc   = doc,
      .input = input,
  };

  if (read_peek(&ctx, XmlPhase_Markup).type == XmlTokenType_DeclStart) {
    read_decl(&ctx, res);
  }

  return ctx.input;
}
