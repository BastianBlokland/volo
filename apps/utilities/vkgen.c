#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "core_path.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "net_http.h"
#include "net_init.h"
#include "net_result.h"
#include "xml_doc.h"
#include "xml_read.h"

/**
 * VulkanGen - Utility to generate a Vulkan api header.
 */

#define VKGEN_VISIT_HASHES                                                                         \
  VKGEN_HASH(bitmask)                                                                              \
  VKGEN_HASH(bitpos)                                                                               \
  VKGEN_HASH(category)                                                                             \
  VKGEN_HASH(comment)                                                                              \
  VKGEN_HASH(constants)                                                                            \
  VKGEN_HASH(deprecated)                                                                           \
  VKGEN_HASH(enum)                                                                                 \
  VKGEN_HASH(enums)                                                                                \
  VKGEN_HASH(member)                                                                               \
  VKGEN_HASH(name)                                                                                 \
  VKGEN_HASH(struct)                                                                               \
  VKGEN_HASH(type)                                                                                 \
  VKGEN_HASH(types)                                                                                \
  VKGEN_HASH(value)

#define VKGEN_HASH(_NAME_) static StringHash g_hash_##_NAME_;
VKGEN_VISIT_HASHES
#undef VKGEN_HASH

static void vkgen_init_hashes(void) {
#define VKGEN_HASH(_NAME_) g_hash_##_NAME_ = string_hash_lit(#_NAME_);
  VKGEN_VISIT_HASHES
#undef VKGEN_HASH
}

static XmlNode vkgen_schema_get(XmlDoc* xmlDoc, const String host, const String uri) {
  XmlNode node = sentinel_u32;

  NetHttp*  http   = net_http_connect_sync(g_allocHeap, host, NetHttpFlags_TlsNoVerify);
  DynString buffer = dynstring_create(g_allocHeap, usize_mebibyte * 4);

  const NetResult netRes = net_http_get_sync(http, uri, null /* auth */, null /* etag */, &buffer);
  if (netRes != NetResult_Success) {
    const String errMsg = net_result_str(netRes);
    log_e("Failed to download Vulkan schema", log_param("error", fmt_text(errMsg)));
    goto Ret;
  }
  XmlResult xmlRes;
  xml_read(xmlDoc, dynstring_view(&buffer), &xmlRes);
  if (xmlRes.type != XmlResultType_Success) {
    const String errMsg = xml_error_str(xmlRes.error);
    log_e("Failed to parse Vulkan schema", log_param("error", fmt_text(errMsg)));
    goto Ret;
  }
  node = xmlRes.node;

  dynstring_destroy(&buffer);
  net_http_shutdown_sync(http);
  net_http_destroy(http);

Ret:
  return node;
}

typedef struct {
  XmlDoc*    schemaDoc;
  XmlNode    schemaRoot;
  String     schemaUri;
  DynString* out;
} VkGenContext;

static void vkgen_comment_elem(VkGenContext* ctx, const XmlNode comment) {
  const XmlNode text = xml_first_child(ctx->schemaDoc, comment);
  if (xml_is(ctx->schemaDoc, text, XmlType_Text)) {
    const String str = xml_value(ctx->schemaDoc, text);
    fmt_write(ctx->out, "// {}.\n", fmt_text(str, .flags = FormatTextFlags_SingleLine));
  }
}

static void vkgen_prolog(VkGenContext* ctx) {
  fmt_write(ctx->out, "#pragma once\n");
  fmt_write(
      ctx->out,
      "// Generated by '{}' from '{}'.\n",
      fmt_text(path_filename(g_pathExecutable)),
      fmt_text(ctx->schemaUri));

  const XmlNode copyrightElem = xml_first_child(ctx->schemaDoc, ctx->schemaRoot);
  if (xml_is(ctx->schemaDoc, copyrightElem, XmlType_Element)) {
    vkgen_comment_elem(ctx, copyrightElem);
  }

  fmt_write(ctx->out, "// clang-format off\n\n");
  fmt_write(ctx->out, "#include \"core.h\"\n");
}

static void vkgen_epilog(VkGenContext* ctx) { fmt_write(ctx->out, "// clang-format on\n"); }

static void vkgen_enum(VkGenContext* ctx, const XmlNode enumNode) {
  const String     enumName = xml_attr_get(ctx->schemaDoc, enumNode, g_hash_name);
  const StringHash typeHash = xml_attr_get_hash(ctx->schemaDoc, enumNode, g_hash_type);
  if (sentinel_check(xml_first_child(ctx->schemaDoc, enumNode))) {
    return; // Empty enum.
  }
  if (typeHash == g_hash_enum || typeHash == g_hash_bitmask) {
    fmt_write(ctx->out, "typedef enum {\n", fmt_text(enumName));
    xml_for_children(ctx->schemaDoc, enumNode, entry) {
      if (xml_name_hash(ctx->schemaDoc, entry) != g_hash_enum) {
        continue; // Not an enum entry.
      }
      if (!string_is_empty(xml_attr_get(ctx->schemaDoc, entry, g_hash_deprecated))) {
        continue; // Is deprecated.
      }
      const String name = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
      fmt_write(ctx->out, "  {}", fmt_text(name));

      const String val    = xml_attr_get(ctx->schemaDoc, entry, g_hash_value);
      const String bitPos = xml_attr_get(ctx->schemaDoc, entry, g_hash_bitpos);
      if (!string_is_empty(val)) {
        fmt_write(ctx->out, " = {}", fmt_text(val));
      } else if (!string_is_empty(bitPos)) {
        fmt_write(ctx->out, " = 1 << {}", fmt_text(bitPos));
      }
      fmt_write(ctx->out, ",");

      const String comment = xml_attr_get(ctx->schemaDoc, entry, g_hash_comment);
      if (!string_is_empty(comment)) {
        fmt_write(ctx->out, " // {}", fmt_text(comment));
      }

      fmt_write(ctx->out, "\n");
    }
    fmt_write(ctx->out, "} {};\n\n", fmt_text(enumName));
  } else if (typeHash == g_hash_constants) {
    xml_for_children(ctx->schemaDoc, enumNode, entry) {
      if (xml_name_hash(ctx->schemaDoc, entry) != g_hash_enum) {
        continue; // Not an enum entry.
      }
      const String name = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
      const String val  = xml_attr_get(ctx->schemaDoc, entry, g_hash_value);
      fmt_write(ctx->out, "#define {} {}\n", fmt_text(name), fmt_text(val));
    }
    fmt_write(ctx->out, "\n");
  }
}

static void vkgen_type(VkGenContext* ctx, const XmlNode typeNode) {
  const String     structName   = xml_attr_get(ctx->schemaDoc, typeNode, g_hash_name);
  const StringHash categoryHash = xml_attr_get_hash(ctx->schemaDoc, typeNode, g_hash_category);
  if (categoryHash == g_hash_struct) {
    fmt_write(ctx->out, "typedef struct {} {\n", fmt_text(structName));
    xml_for_children(ctx->schemaDoc, typeNode, entry) {
      if (xml_name_hash(ctx->schemaDoc, entry) != g_hash_member) {
        continue; // Not a struct member.
      }
      fmt_write(ctx->out, "  ");
      bool first = true;
      xml_for_children(ctx->schemaDoc, entry, part) {
        if (!first) {
          fmt_write(ctx->out, " ");
        }
        first = false;
        switch (xml_type(ctx->schemaDoc, part)) {
        case XmlType_Element:
          fmt_write(ctx->out, "{}", fmt_text(xml_child_text(ctx->schemaDoc, part)));
          break;
        case XmlType_Text:
          fmt_write(ctx->out, "{}", fmt_text(xml_value(ctx->schemaDoc, part)));
          break;
        default:
          break;
        }
      }
      fmt_write(ctx->out, ";\n");
    }
    fmt_write(ctx->out, "} {};\n\n", fmt_text(structName));
  }
}

static bool vkgen_generate(VkGenContext* ctx) {
  vkgen_prolog(ctx);
  fmt_write(ctx->out, "\n");

  // Generate enums.
  xml_for_children(ctx->schemaDoc, ctx->schemaRoot, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_enums) {
      vkgen_enum(ctx, child);
    }
  }

  // Generate types.
  const XmlNode types = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_types);
  xml_for_children(ctx->schemaDoc, types, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_type) {
      vkgen_type(ctx, child);
    }
  }

  vkgen_epilog(ctx);
  return true;
}

// clang-format off
static const String g_schemaDefaultHost = string_static("raw.githubusercontent.com");
static const String g_schemaDefaultUri  = string_static("/KhronosGroup/Vulkan-Docs/refs/tags/v1.4.308/xml/vk.xml");
// clang-format on

static CliId g_optVerbose, g_optOutputPath, g_optSchemaHost, g_optSchemaUri, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("VulkanGen - Utility to generate a Vulkan api header."));

  g_optVerbose = cli_register_flag(app, 'v', string_lit("verbose"), CliOptionFlags_None);

  g_optOutputPath = cli_register_arg(app, string_lit("output-path"), CliOptionFlags_Required);
  cli_register_desc(app, g_optOutputPath, string_lit("Path where to write the header to."));

  g_optSchemaHost = cli_register_flag(app, '\0', string_lit("schema-host"), CliOptionFlags_Value);
  cli_register_desc(app, g_optSchemaHost, string_lit("Host of the Vulkan schema."));

  g_optSchemaUri = cli_register_flag(app, '\0', string_lit("schema-uri"), CliOptionFlags_Value);
  cli_register_desc(app, g_optSchemaUri, string_lit("Uri of the Vulkan schema."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optOutputPath);
  cli_register_exclusions(app, g_optHelp, g_optVerbose);
  cli_register_exclusions(app, g_optHelp, g_optSchemaHost);
  cli_register_exclusions(app, g_optHelp, g_optSchemaUri);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }
  vkgen_init_hashes();
  net_init();

  bool success = false;

  const LogMask logMask = cli_parse_provided(invoc, g_optVerbose) ? LogMask_All : ~LogMask_Debug;
  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, logMask));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const String outputPath = cli_read_string(invoc, g_optOutputPath, string_empty);
  const String host       = cli_read_string(invoc, g_optSchemaHost, g_schemaDefaultHost);
  const String uri        = cli_read_string(invoc, g_optSchemaUri, g_schemaDefaultUri);

  XmlDoc*   xmlDoc       = xml_create(g_allocHeap, 1024);
  DynString outputBuffer = dynstring_create(g_allocHeap, usize_kibibyte * 16);

  const XmlNode schemaRoot = vkgen_schema_get(xmlDoc, host, uri);
  if (sentinel_check(schemaRoot)) {
    goto Exit;
  }

  VkGenContext ctx = {
      .schemaDoc  = xmlDoc,
      .schemaRoot = schemaRoot,
      .schemaUri  = uri,
      .out        = &outputBuffer,
  };
  if (vkgen_generate(&ctx)) {
    success = true;
  }

Exit:
  if (success && !string_is_empty(outputPath)) {
    if (file_write_to_path_sync(outputPath, dynstring_view(&outputBuffer)) == FileResult_Success) {
      log_i("Generated header", log_param("path", fmt_path(outputPath)));
    }
  }
  dynstring_destroy(&outputBuffer);
  xml_destroy(xmlDoc);

  net_teardown();
  return success ? 0 : 1;
}
