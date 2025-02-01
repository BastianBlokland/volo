#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_file.h"
#include "core_path.h"
#include "data_read.h"
#include "data_utils.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "net_http.h"
#include "net_init.h"
#include "net_rest.h"
#include "net_result.h"

/**
 * Fetch - Utility to download external assets.
 */

#define fetch_worker_count 4

typedef struct {
  String host;
  String rootUri;
  String authUser, authPass;
  HeapArray_t(String) assets;
} FetchOrigin;

typedef struct {
  String targetPath;
  HeapArray_t(FetchOrigin) origins;
} FetchConfig;

static DataMeta g_fetchConfigMeta;

static void fetch_data_init(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, FetchOrigin);
  data_reg_field_t(g_dataReg, FetchOrigin, host, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, FetchOrigin, rootUri, data_prim_t(String));
  data_reg_field_t(g_dataReg, FetchOrigin, authUser, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, FetchOrigin, authPass, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, FetchOrigin, assets, data_prim_t(String), .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, FetchConfig);
  data_reg_field_t(g_dataReg, FetchConfig, targetPath, data_prim_t(String));
  data_reg_field_t(g_dataReg, FetchConfig, origins, t_FetchOrigin, .container = DataContainer_HeapArray);
  // clang-format on

  g_fetchConfigMeta = data_meta_t(t_FetchConfig);
}

static bool fetch_config_load(const String path, FetchConfig* out) {
  // Open the file handle.
  bool       success = false;
  File*      file    = null;
  FileResult fileRes;
  if ((fileRes = file_create(g_allocScratch, path, FileMode_Open, FileAccess_Read, &file))) {
    log_e("Failed to open config file", log_param("err", fmt_text(file_result_str(fileRes))));
    goto Ret;
  }

  // Map the file data.
  String fileData;
  if (UNLIKELY(fileRes = file_map(file, &fileData, FileHints_Prefetch))) {
    log_e("Failed to map config file", log_param("err", fmt_text(file_result_str(fileRes))));
    goto Ret;
  }

  // Parse the json.
  DataReadResult result;
  const Mem      outMem = mem_create(out, sizeof(FetchConfig));
  data_read_json(g_dataReg, fileData, g_allocHeap, g_fetchConfigMeta, outMem, &result);
  if (UNLIKELY(result.error)) {
    log_e("Failed to parse config file", log_param("err", fmt_text(result.errorMsg)));
    goto Ret;
  }
  success = true;

Ret:
  if (file) {
    file_destroy(file);
  }
  return success;
}

static void fetch_config_destroy(FetchConfig* cfg) {
  data_destroy(g_dataReg, g_allocHeap, g_fetchConfigMeta, mem_create(cfg, sizeof(FetchConfig)));
}

static u32 fetch_config_asset_count(FetchConfig* cfg) {
  u32 res = 0;
  heap_array_for_t(cfg->origins, FetchOrigin, origin) { res += (u32)origin->assets.count; }
  return res;
}

typedef struct {
  String configPath;
} FetchContext;

static NetHttpFlags fetch_http_flags(void) {
  /**
   * Enable Tls transport but do not enable certificate validation.
   * This means traffic is encrypted and people cannot eavesdrop, however its trivial for someone
   * to man-in-the-middle as we do not verify the server's authenticity.
   * Please do not use this for security sensitive applications!
   */
  return NetHttpFlags_TlsNoVerify;
}

static i32 fetch_run(FetchContext* ctx) {
  NetRest*    rest = null;
  FetchConfig cfg;
  if (!fetch_config_load(ctx->configPath, &cfg)) {
    return 1;
  }
  const u32 assetCount = fetch_config_asset_count(&cfg);
  if (!assetCount) {
    goto Done;
  }
  rest = net_rest_create(g_allocHeap, fetch_worker_count, assetCount, fetch_http_flags());

  const String targetPath = path_build_scratch(ctx->configPath, cfg.targetPath);
  (void)targetPath;

Done:
  if (rest) {
    net_rest_destroy(rest);
  }
  fetch_config_destroy(&cfg);
  return 0;
}

static CliId g_optConfigPath, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Fetch utility."));

  g_optConfigPath = cli_register_arg(app, string_lit("config"), CliOptionFlags_Required);
  cli_register_desc(app, g_optConfigPath, string_lit("Path to a fetch config file."));
  cli_register_validator(app, g_optConfigPath, cli_validate_file_regular);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optConfigPath);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  i32 retCode = 0;
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return retCode;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  FetchContext ctx = {
      .configPath = cli_read_string(invoc, g_optConfigPath, string_empty),
  };

  fetch_data_init();
  net_init();

  retCode = fetch_run(&ctx);

  net_teardown();
  return retCode;
}
