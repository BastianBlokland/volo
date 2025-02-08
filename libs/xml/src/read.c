#include "core_sentinel.h"
#include "core_string.h"
#include "xml_doc.h"
#include "xml_read.h"

#include "lex_internal.h"

typedef struct {
  XmlDoc* doc;
  String  input;
} XmlReadContext;

static XmlResult xml_err(const XmlError err) {
  return (XmlResult){.type = XmlResultType_Fail, .error = err};
}

static XmlResult xml_error_from_token(const XmlToken token) {
  if (token.type == XmlTokenType_Error) {
    return xml_err(token.val_error);
  }
  return xml_err(XmlError_UnexpectedToken);
}

static XmlResult xml_success(const XmlNode node) {
  return (XmlResult){.type = XmlResultType_Success, .node = node};
}

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

static void read_attribute(XmlReadContext* ctx, const XmlNode parent, XmlResult* res) {
  XmlToken nameToken = read_consume(ctx, XmlPhase_Markup);
  if (nameToken.type != XmlTokenType_Name) {
    *res = xml_error_from_token(nameToken);
    return;
  }
  XmlToken equalToken = read_consume(ctx, XmlPhase_Markup);
  if (equalToken.type != XmlTokenType_Equal) {
    *res = xml_error_from_token(equalToken);
    return;
  }
  XmlToken valueToken = read_consume(ctx, XmlPhase_Markup);
  if (equalToken.type != XmlTokenType_String) {
    *res = xml_error_from_token(valueToken);
    return;
  }
  const XmlNode node = xml_add_attr(ctx->doc, parent, nameToken.val_name, valueToken.val_string);
  if (sentinel_check(node)) {
    *res = xml_err(XmlError_InvalidAttribute);
  } else {
    *res = xml_success(node);
  }
}

static void read_decl(XmlReadContext* ctx, XmlResult* res) {
  XmlToken startToken = read_consume(ctx, XmlPhase_Markup);
  if (startToken.type != XmlTokenType_DeclStart) {
    *res = xml_error_from_token(startToken);
    return;
  }
  if (!string_eq(startToken.val_decl, string_lit("xml"))) {
    *res = xml_error_from_token(startToken);
    return;
  }
  const XmlNode node = xml_add_elem(ctx->doc, sentinel_u32 /* parent */, startToken.val_decl);
  for (;;) {
    const XmlToken nextToken = read_peek(ctx, XmlPhase_Markup);
    if (nextToken.type == XmlTokenType_DeclClose) {
      *res = xml_success(node);
      return;
    }
    read_attribute(ctx, node, res);
    if (res->type == XmlResultType_Fail) {
      return;
    }
  }
}

static void read_elem(XmlReadContext* ctx, const XmlNode parent, XmlResult* res) {
  XmlToken startToken = read_consume(ctx, XmlPhase_Markup);
  if (startToken.type != XmlTokenType_TagStart) {
    *res = xml_error_from_token(startToken);
    return;
  }
  const XmlNode node = xml_add_elem(ctx->doc, parent, startToken.val_tag);

  // Read attributes.
  for (;;) {
    const XmlToken nextToken = read_peek(ctx, XmlPhase_Markup);
    if (nextToken.type == XmlTokenType_TagEndClose) {
      read_consume(ctx, XmlPhase_Markup);
      *res = xml_success(node);
      return;
    }
    if (nextToken.type == XmlTokenType_TagClose) {
      read_consume(ctx, XmlPhase_Markup);
      break;
    }
    read_attribute(ctx, node, res);
    if (res->type == XmlResultType_Fail) {
      return;
    }
  }

  // Read content.
  for (;;) {
    const XmlToken nextToken = read_peek(ctx, XmlPhase_Content);
    if (nextToken.type == XmlTokenType_Content) {
      read_consume(ctx, XmlPhase_Content); // TODO: Avoid lexing this twice.
      xml_add_text(ctx->doc, node, nextToken.val_content);
      continue;
    }
    if (nextToken.type == XmlTokenType_Comment) {
      read_consume(ctx, XmlPhase_Content); // TODO: Avoid lexing this twice.
      xml_add_comment(ctx->doc, node, nextToken.val_content);
      continue;
    }
    if (nextToken.type == XmlTokenType_TagStart) {
      read_elem(ctx, node, res);
      if (res->type == XmlResultType_Fail) {
        return;
      }
      continue;
    }
    if (nextToken.type != XmlTokenType_TagEnd) {
      *res = xml_error_from_token(startToken);
      return;
    }
    if (!string_eq(nextToken.val_tag, startToken.val_tag)) {
      *res = xml_err(XmlError_MismatchedEndTag);
      return;
    }
    read_consume(ctx, XmlPhase_Content); // Avoid lexing this twice.
    *res = xml_success(node);
    return;
  }
}

String xml_read(XmlDoc* doc, const String input, XmlResult* res) {
  XmlReadContext ctx = {
      .doc   = doc,
      .input = input,
  };

  // Optionally read an xml declaration.
  if (read_peek(&ctx, XmlPhase_Markup).type == XmlTokenType_DeclStart) {
    read_decl(&ctx, res);
    if (res->type == XmlResultType_Fail) {
      return ctx.input;
    }
  }

  // Read the root element.
  read_elem(&ctx, sentinel_u32 /* parent */, res);

  return ctx.input;
}
