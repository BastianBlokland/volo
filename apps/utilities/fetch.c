#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "core_math.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_time.h"
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

#define fetch_worker_count 2

typedef struct {
  String host;
  String license;
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
  data_reg_field_t(g_dataReg, FetchOrigin, license, data_prim_t(String), .flags = DataFlags_Opt);
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

static u32 fetch_config_max_origin_assets(FetchConfig* cfg) {
  u32 res = 0;
  heap_array_for_t(cfg->origins, FetchOrigin, origin) {
    res = math_max(res, (u32)origin->assets.count);
  }
  return res;
}

static String fetch_config_uri_scratch(const FetchOrigin* origin, const String asset) {
  DynString result = dynstring_create(g_allocScratch, 256);
  if (!string_starts_with(origin->rootUri, string_lit("/"))) {
    dynstring_append_char(&result, '/');
  }
  dynstring_append(&result, origin->rootUri);
  if (!string_ends_with(dynstring_view(&result), string_lit("/"))) {
    dynstring_append_char(&result, '/');
  }
  if (string_starts_with(asset, string_lit("/"))) {
    dynstring_append(&result, string_consume(asset, 1));
  } else {
    dynstring_append(&result, asset);
  }
  return dynstring_view(&result);
}

static NetHttpFlags fetch_http_flags(void) {
  /**
   * Enable Tls transport but do not enable certificate validation.
   * This means traffic is encrypted and people cannot eavesdrop, however its trivial for someone
   * to man-in-the-middle as we do not verify the server's authenticity.
   * Please do not use this for security sensitive applications!
   */
  return NetHttpFlags_TlsNoVerify;
}

static i32 fetch_run_origin(NetRest* rest, const String targetPath, const FetchOrigin* org) {
  i32 retCode = 0;

  NetHttpAuth auth = {0};
  if (!string_is_empty(org->authUser)) {
    auth = (NetHttpAuth){.type = NetHttpAuthType_Basic, .user = org->authUser, .pw = org->authPass};
  }

  NetRestId* requests = alloc_array_t(g_allocHeap, NetRestId, org->assets.count);

  // Start a GET request for all assets.
  for (u32 i = 0; i != org->assets.count; ++i) {
    const String uri = fetch_config_uri_scratch(org, org->assets.values[i]);
    requests[i]      = net_rest_get(rest, org->host, uri, &auth, null);
  }

  // Save the results.
  for (u32 i = 0; i != org->assets.count; ++i) {
    const NetRestId request = requests[i];
    const String    asset   = org->assets.values[i];

    // Wait for the request to be done.
    while (!net_rest_done(rest, request)) {
      thread_sleep(time_milliseconds(100));
    }

    // Save the asset to disk.
    const NetResult result = net_rest_result(rest, request);
    if (result == NetResult_Success) {
      const String path = path_build_scratch(targetPath, asset);
      const String data = net_rest_data(rest, request);

      FileResult saveRes = file_create_dir_sync(path_parent(path));
      if (saveRes == FileResult_Success) {
        saveRes = file_write_to_path_atomic(path, data);
      }
      if (saveRes != FileResult_Success) {
        log_e(
            "Asset save failed: '{}'",
            log_param("asset", fmt_text(asset)),
            log_param("path", fmt_path(path)),
            log_param("error", fmt_text(file_result_str(saveRes))));
        retCode = 2;
      } else {
        log_i(
            "Asset fetched: '{}'",
            log_param("asset", fmt_text(asset)),
            log_param("size", fmt_size(data.size)));
      }
    } else {
      log_e(
          "Asset fetch failed: '{}'",
          log_param("asset", fmt_text(asset)),
          log_param("error", fmt_text(net_result_str(result))));
      retCode = 1;
    }
    net_rest_release(rest, request);
  }

  alloc_free_array_t(g_allocHeap, requests, org->assets.count);
  return retCode;
}

static i32 fetch_run(FetchConfig* config, const String targetPath) {
  i32              retCode   = 0;
  NetRest*         rest      = null;
  const TimeSteady timeStart = time_steady_clock();

  const u32 maxRequests = fetch_config_max_origin_assets(config);
  if (maxRequests) {
    rest = net_rest_create(g_allocHeap, fetch_worker_count, maxRequests, fetch_http_flags());

    heap_array_for_t(config->origins, FetchOrigin, origin) {
      i32 originRet = 0;
      if (origin->assets.count) {
        originRet = fetch_run_origin(rest, targetPath, origin);
      }
      retCode = math_max(retCode, originRet);
    }
  }
  const TimeDuration duration = time_steady_duration(timeStart, time_steady_clock());
  if (!retCode) {
    log_i("Fetch finished", log_param("duration", fmt_duration(duration)));
  } else {
    log_e("Fetch failed", log_param("duration", fmt_duration(duration)));
  }
  if (rest) {
    net_rest_destroy(rest);
  }
  return retCode;
}

static CliId g_optConfigPath, g_optVerbose, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Fetch utility."));

  g_optConfigPath = cli_register_arg(app, string_lit("config"), CliOptionFlags_Required);
  cli_register_desc(app, g_optConfigPath, string_lit("Path to a fetch config file."));
  cli_register_validator(app, g_optConfigPath, cli_validate_file_regular);

  g_optVerbose = cli_register_flag(app, 'v', string_lit("verbose"), CliOptionFlags_None);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optConfigPath);
  cli_register_exclusions(app, g_optHelp, g_optVerbose);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  i32 retCode = 0;
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return retCode;
  }

  const LogMask logMask = cli_parse_provided(invoc, g_optVerbose) ? LogMask_All : ~LogMask_Debug;
  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, logMask));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  const String configPath = cli_read_string(invoc, g_optConfigPath, string_empty);

  fetch_data_init();

  FetchConfig config;
  if (!fetch_config_load(configPath, &config)) {
    return 1;
  }

  DynString targetPath = dynstring_create(g_allocHeap, 128);
  path_build(&targetPath, path_parent(configPath), config.targetPath);

  net_init();
  retCode = fetch_run(&config, dynstring_view(&targetPath));
  net_teardown();

  dynstring_destroy(&targetPath);
  fetch_config_destroy(&config);
  return retCode;
}
