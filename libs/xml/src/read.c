#include "core_array.h"
#include "core_diag.h"
#include "core_sentinel.h"
#include "core_string.h"
#include "xml_doc.h"
#include "xml_read.h"

#include "lex_internal.h"

#define xml_depth_max 100

typedef struct {
  XmlDoc* doc;
  String  input;
  u32     depth;
} XmlReadContext;

static XmlResult xml_err(const XmlError err) {
  return (XmlResult){.type = XmlResultType_Fail, .error = err};
}

static XmlResult xml_error_from_token(const XmlToken token) {
  if (token.type == XmlTokenType_Error) {
    return xml_err(token.val_error);
  }
  if (token.type == XmlTokenType_End) {
    return xml_err(XmlError_Truncated);
  }
  return xml_err(XmlError_UnexpectedToken);
}

static XmlResult xml_success(const XmlNode node) {
  return (XmlResult){.type = XmlResultType_Success, .node = node};
}

static const String g_errorStrs[] = {
    [XmlError_InvalidDeclStart]         = string_static("InvalidDeclStart"),
    [XmlError_InvalidTagStart]          = string_static("InvalidTagStart"),
    [XmlError_InvalidTagEnd]            = string_static("InvalidTagEnd"),
    [XmlError_InvalidChar]              = string_static("InvalidChar"),
    [XmlError_InvalidCharInContent]     = string_static("InvalidCharInContent"),
    [XmlError_InvalidUtf8]              = string_static("InvalidUtf8"),
    [XmlError_InvalidCommentTerminator] = string_static("InvalidCommentTerminator"),
    [XmlError_InvalidReference]         = string_static("InvalidReference"),
    [XmlError_InvalidDecl]              = string_static("InvalidDecl"),
    [XmlError_InvalidAttribute]         = string_static("InvalidAttribute"),
    [XmlError_UnterminatedString]       = string_static("UnterminatedString"),
    [XmlError_UnterminatedComment]      = string_static("UnterminatedComment"),
    [XmlError_ContentTooLong]           = string_static("ContentTooLong"),
    [XmlError_Truncated]                = string_static("Truncated"),
    [XmlError_UnexpectedToken]          = string_static("UnexpectedToken"),
    [XmlError_MismatchedEndTag]         = string_static("MismatchedEndTag"),
    [XmlError_MaximumDepthExceeded]     = string_static("MaximumDepthExceeded"),
};

ASSERT(array_elems(g_errorStrs) == XmlError_Count, "Incorrect number of XmlError strings");

String xml_error_str(const XmlError error) {
  diag_assert(error < XmlError_Count);
  return g_errorStrs[error];
}

static XmlToken read_consume(XmlReadContext* ctx, const XmlPhase phase) {
  XmlToken token;
  ctx->input = xml_lex(ctx->input, phase, &token);
  return token;
}

static void read_attribute(
    XmlReadContext* ctx, const XmlToken nameToken, const XmlNode parent, XmlResult* res) {
  if (UNLIKELY(nameToken.type != XmlTokenType_Name)) {
    *res = xml_error_from_token(nameToken);
    return;
  }
  XmlToken equalToken = read_consume(ctx, XmlPhase_Markup);
  if (UNLIKELY(equalToken.type != XmlTokenType_Equal)) {
    *res = xml_error_from_token(equalToken);
    return;
  }
  XmlToken valueToken = read_consume(ctx, XmlPhase_Markup);
  if (UNLIKELY(valueToken.type != XmlTokenType_String)) {
    *res = xml_error_from_token(valueToken);
    return;
  }
  const XmlNode node = xml_add_attr(ctx->doc, parent, nameToken.val_name, valueToken.val_string);
  if (UNLIKELY(sentinel_check(node))) {
    *res = xml_err(XmlError_InvalidAttribute);
  } else {
    *res = xml_success(node);
  }
}

static void read_decl(XmlReadContext* ctx, const XmlToken startToken, XmlResult* res) {
  if (UNLIKELY(startToken.type != XmlTokenType_DeclStart)) {
    *res = xml_error_from_token(startToken);
    return;
  }
  if (UNLIKELY(!string_eq(startToken.val_decl, string_lit("xml")))) {
    *res = xml_error_from_token(startToken);
    return;
  }
  const XmlNode node = xml_add_elem(ctx->doc, sentinel_u32 /* parent */, startToken.val_decl);
  for (;;) {
    const XmlToken token = read_consume(ctx, XmlPhase_Markup);
    if (token.type == XmlTokenType_DeclClose) {
      *res = xml_success(node);
      return;
    }
    read_attribute(ctx, token, node, res);
    if (res->type == XmlResultType_Fail) {
      return;
    }
  }
}

static void
read_elem(XmlReadContext* ctx, const XmlToken startToken, const XmlNode parent, XmlResult* res) {
  if (UNLIKELY(startToken.type != XmlTokenType_TagStart)) {
    *res = xml_error_from_token(startToken);
    return;
  }

  if (++ctx->depth > xml_depth_max) {
    *res = xml_err(XmlError_MaximumDepthExceeded);
    goto Ret;
  }

  const XmlNode node = xml_add_elem(ctx->doc, parent, startToken.val_tag);

  // Read attributes.
  for (;;) {
    const XmlToken token = read_consume(ctx, XmlPhase_Markup);
    if (token.type == XmlTokenType_TagEndClose) {
      *res = xml_success(node);
      goto Ret;
    }
    if (token.type == XmlTokenType_TagClose) {
      break;
    }
    read_attribute(ctx, token, node, res);
    if (UNLIKELY(res->type == XmlResultType_Fail)) {
      goto Ret;
    }
  }

  // Read content.
  for (;;) {
    const XmlToken token = read_consume(ctx, XmlPhase_Content);
    if (token.type == XmlTokenType_Content) {
      xml_add_text(ctx->doc, node, token.val_content);
      continue;
    }
    if (token.type == XmlTokenType_Comment) {
      xml_add_comment(ctx->doc, node, token.val_content);
      continue;
    }
    if (token.type == XmlTokenType_TagStart) {
      read_elem(ctx, token, node, res);
      if (UNLIKELY(res->type == XmlResultType_Fail)) {
        goto Ret;
      }
      continue;
    }
    if (UNLIKELY(token.type != XmlTokenType_TagEnd)) {
      *res = xml_error_from_token(startToken);
      goto Ret;
    }
    if (UNLIKELY(!string_eq(token.val_tag, startToken.val_tag))) {
      *res = xml_err(XmlError_MismatchedEndTag);
      goto Ret;
    }
    *res = xml_success(node);
    goto Ret;
  }

Ret:
  --ctx->depth;
}

String xml_read(XmlDoc* doc, const String input, XmlResult* res) {
  XmlReadContext ctx = {
      .doc   = doc,
      .input = input,
  };

  XmlToken startToken = read_consume(&ctx, XmlPhase_Markup);

  // Optionally read an xml declaration.
  if (startToken.type == XmlTokenType_DeclStart) {
    read_decl(&ctx, startToken, res);
    if (res->type == XmlResultType_Fail) {
      return ctx.input;
    }
    startToken = read_consume(&ctx, XmlPhase_Markup);
  }

  // Read the root element.
  read_elem(&ctx, startToken, sentinel_u32 /* parent */, res);

  return ctx.input;
}
