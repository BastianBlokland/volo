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
  VKGEN_HASH(command)                                                                              \
  VKGEN_HASH(commands)                                                                             \
  VKGEN_HASH(comment)                                                                              \
  VKGEN_HASH(constants)                                                                            \
  VKGEN_HASH(deprecated)                                                                           \
  VKGEN_HASH(enum)                                                                                 \
  VKGEN_HASH(enums)                                                                                \
  VKGEN_HASH(feature)                                                                              \
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

  log_i("Downloading schema", log_param("host", fmt_text(host)), log_param("uri", fmt_text(uri)));

  NetHttp*  http   = net_http_connect_sync(g_allocHeap, host, NetHttpFlags_TlsNoVerify);
  DynString buffer = dynstring_create(g_allocHeap, usize_mebibyte * 4);

  const NetResult netRes = net_http_get_sync(http, uri, null /* auth */, null /* etag */, &buffer);
  if (netRes != NetResult_Success) {
    const String errMsg = net_result_str(netRes);
    log_e("Failed to download Vulkan schema", log_param("error", fmt_text(errMsg)));
    goto Ret;
  }
  log_i("Downloaded schema", log_param("size", fmt_size(buffer.size)));

  XmlResult xmlRes;
  xml_read(xmlDoc, dynstring_view(&buffer), &xmlRes);
  if (xmlRes.type != XmlResultType_Success) {
    const String errMsg = xml_error_str(xmlRes.error);
    log_e("Failed to parse Vulkan schema", log_param("error", fmt_text(errMsg)));
    goto Ret;
  }
  node = xmlRes.node;

  log_i("Parsed schema");

  dynstring_destroy(&buffer);
  net_http_shutdown_sync(http);
  net_http_destroy(http);

Ret:
  return node;
}

typedef struct {
  StringHash key;
  XmlNode    schemaNode;
} VkGenEntry;

typedef struct {
  XmlDoc*   schemaDoc;
  XmlNode   schemaRoot;
  String    schemaHost, schemaUri;
  DynArray  types;    // VkGenType[]
  DynArray  enums;    // VkGenType[]
  DynArray  commands; // VkGenType[]
  DynArray  features; // VkGenType[]
  DynString out;
} VkGenContext;

static i8 vkgen_compare_entry(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, VkGenEntry, key), field_ptr(b, VkGenEntry, key));
}

static void vkgen_entry_push(DynArray* arr, const StringHash key, const XmlNode node) {
  *dynarray_push_t(arr, VkGenEntry) = (VkGenEntry){
      .key        = key,
      .schemaNode = node,
  };
}

static XmlNode vkgen_entry_find(DynArray* arr, const StringHash key) {
  const VkGenEntry tgt = {.key = key};
  VkGenEntry*      res = dynarray_search_binary(arr, vkgen_compare_entry, &tgt);
  return res ? res->schemaNode : sentinel_u32;
}

static void vkgen_collect_types(VkGenContext* ctx) {
  const XmlNode typesNode = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_types);
  xml_for_children(ctx->schemaDoc, typesNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_type) {
      const StringHash nameHash = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
      if (nameHash) {
        vkgen_entry_push(&ctx->types, nameHash, child);
      }
    }
  }
  dynarray_sort(&ctx->types, vkgen_compare_entry);
  log_i("Collected types", log_param("count", fmt_int(ctx->types.size)));
}

static void vkgen_collect_enums(VkGenContext* ctx) {
  xml_for_children(ctx->schemaDoc, ctx->schemaRoot, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_enums) {
      const StringHash nameHash = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
      if (nameHash) {
        vkgen_entry_push(&ctx->enums, nameHash, child);
      }
    }
  }
  dynarray_sort(&ctx->enums, vkgen_compare_entry);
  log_i("Collected enums", log_param("count", fmt_int(ctx->enums.size)));
}

static void vkgen_collect_commands(VkGenContext* ctx) {
  const XmlNode commandsNode = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_commands);
  xml_for_children(ctx->schemaDoc, commandsNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_command) {
      const StringHash nameHash = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
      if (nameHash) {
        vkgen_entry_push(&ctx->commands, nameHash, child);
      }
    }
  }
  dynarray_sort(&ctx->commands, vkgen_compare_entry);
  log_i("Collected commands", log_param("count", fmt_int(ctx->commands.size)));
}

static void vkgen_collect_features(VkGenContext* ctx) {
  xml_for_children(ctx->schemaDoc, ctx->schemaRoot, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_feature) {
      const StringHash nameHash = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
      if (nameHash) {
        vkgen_entry_push(&ctx->features, nameHash, child);
      }
    }
  }
  dynarray_sort(&ctx->features, vkgen_compare_entry);
  log_i("Collected features", log_param("count", fmt_int(ctx->features.size)));
}

static void vkgen_write_comment_elem(VkGenContext* ctx, const XmlNode comment) {
  const XmlNode text = xml_first_child(ctx->schemaDoc, comment);
  if (xml_is(ctx->schemaDoc, text, XmlType_Text)) {
    const String str = xml_value(ctx->schemaDoc, text);
    fmt_write(&ctx->out, "// {}.\n", fmt_text(str, .flags = FormatTextFlags_SingleLine));
  }
}

static void vkgen_write_prolog(VkGenContext* ctx) {
  fmt_write(&ctx->out, "#pragma once\n");
  fmt_write(
      &ctx->out,
      "// Generated by '{}' from '{}{}'.\n",
      fmt_text(path_stem(g_pathExecutable)),
      fmt_text(ctx->schemaHost),
      fmt_text(ctx->schemaUri));

  const XmlNode copyrightElem = xml_first_child(ctx->schemaDoc, ctx->schemaRoot);
  if (xml_is(ctx->schemaDoc, copyrightElem, XmlType_Element)) {
    vkgen_write_comment_elem(ctx, copyrightElem);
  }

  fmt_write(&ctx->out, "// clang-format off\n\n");
  fmt_write(&ctx->out, "#include \"core.h\"\n");
}

static void vkgen_write_epilog(VkGenContext* ctx) { fmt_write(&ctx->out, "// clang-format on\n"); }

static bool vkgen_write_constants(VkGenContext* ctx) {
  const XmlNode apiConstants = vkgen_entry_find(&ctx->enums, string_hash_lit("API Constants"));
  if (sentinel_check(apiConstants)) {
    return false;
  }
  xml_for_children(ctx->schemaDoc, apiConstants, entry) {
    if (xml_name_hash(ctx->schemaDoc, entry) == g_hash_enum) {
      const String name = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
      const String val  = xml_attr_get(ctx->schemaDoc, entry, g_hash_value);
      fmt_write(&ctx->out, "#define {} {}\n", fmt_text(name), fmt_text(val));
    }
  }
  fmt_write(&ctx->out, "\n");
  return true;
}

static bool vkgen_write_header(VkGenContext* ctx) {
  vkgen_write_prolog(ctx);
  fmt_write(&ctx->out, "\n");

  if (!vkgen_write_constants(ctx)) {
    return false;
  }

  vkgen_write_epilog(ctx);
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

  VkGenContext ctx = {
      .schemaDoc  = xml_create(g_allocHeap, 128 * 1024),
      .schemaHost = cli_read_string(invoc, g_optSchemaHost, g_schemaDefaultHost),
      .schemaUri  = cli_read_string(invoc, g_optSchemaUri, g_schemaDefaultUri),
      .types      = dynarray_create_t(g_allocHeap, VkGenEntry, 2048),
      .enums      = dynarray_create_t(g_allocHeap, VkGenEntry, 512),
      .commands   = dynarray_create_t(g_allocHeap, VkGenEntry, 128),
      .features   = dynarray_create_t(g_allocHeap, VkGenEntry, 16),
      .out        = dynstring_create(g_allocHeap, usize_kibibyte * 16),
  };

  ctx.schemaRoot = vkgen_schema_get(ctx.schemaDoc, ctx.schemaHost, ctx.schemaUri);
  if (sentinel_check(ctx.schemaRoot)) {
    goto Exit;
  }

  vkgen_collect_types(&ctx);
  vkgen_collect_enums(&ctx);
  vkgen_collect_commands(&ctx);
  vkgen_collect_features(&ctx);

  if (vkgen_write_header(&ctx)) {
    success = true;
  }

Exit:;
  const String outputPath = cli_read_string(invoc, g_optOutputPath, string_empty);
  if (success && !string_is_empty(outputPath)) {
    if (file_write_to_path_sync(outputPath, dynstring_view(&ctx.out)) == FileResult_Success) {
      log_i("Generated header", log_param("path", fmt_path(outputPath)));
    }
  }
  xml_destroy(ctx.schemaDoc);
  dynarray_destroy(&ctx.types);
  dynarray_destroy(&ctx.enums);
  dynarray_destroy(&ctx.commands);
  dynarray_destroy(&ctx.features);
  dynstring_destroy(&ctx.out);

  net_teardown();
  return success ? 0 : 1;
}
