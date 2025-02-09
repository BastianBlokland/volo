#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_file.h"
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

static CliId g_optVerbose, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("VulkanGen - Utility to generate a Vulkan api header."));

  g_optVerbose = cli_register_flag(app, 'v', string_lit("verbose"), CliOptionFlags_None);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optVerbose);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }
  net_init();

  i32 exitCode = 0;

  const LogMask logMask = cli_parse_provided(invoc, g_optVerbose) ? LogMask_All : ~LogMask_Debug;
  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, logMask));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const String host = string_lit("raw.githubusercontent.com");
  const String uri  = string_lit("KhronosGroup/Vulkan-Docs/refs/tags/v1.4.308/xml/vk.xml");

  XmlDoc*   xmlDoc       = xml_create(g_allocHeap, 1024);
  DynString outputBuffer = dynstring_create(g_allocHeap, usize_kibibyte * 16);

  const XmlNode schemaRoot = vkgen_schema_get(xmlDoc, host, uri);
  if (sentinel_check(schemaRoot)) {
    exitCode = 1;
    goto Exit;
  }

Exit:
  dynstring_destroy(&outputBuffer);
  xml_destroy(xmlDoc);

  net_teardown();
  return 0;
}
