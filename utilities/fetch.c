#include "app_cli.h"
#include "cli_app.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "core_math.h"
#include "core_path.h"
#include "core_signal.h"
#include "core_thread.h"
#include "core_time.h"
#include "data_read.h"
#include "data_utils.h"
#include "data_write.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "net_addr.h"
#include "net_http.h"
#include "net_init.h"
#include "net_rest.h"
#include "net_result.h"
#include "net_stats.h"

/**
 * Fetch - Utility to download external assets.
 */

#define fetch_worker_count 2

typedef enum {
  FetchFlags_Force = 1 << 0,
} FetchFlags;

typedef struct {
  String       host;
  String       license;
  String       rootUri;
  TimeDuration cacheTime;
  String       authUser, authPass;
  HeapArray_t(String) assets;
} FetchOrigin;

typedef struct {
  String outputPath;
  HeapArray_t(FetchOrigin) origins;
} FetchConfig;

typedef struct {
  StringHash  pathHash;
  NetHttpEtag etag;
  TimeReal    lastSyncTime;
} FetchRegistryEntry;

typedef struct {
  DynArray entries; // FetchRegistryEntry[], sorted on pathHash.
} FetchRegistry;

static DataMeta g_fetchConfigMeta, g_fetchRegistryMeta;

static void fetch_data_init(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, FetchOrigin);
  data_reg_field_t(g_dataReg, FetchOrigin, host, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, FetchOrigin, license, data_prim_t(String), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, FetchOrigin, rootUri, data_prim_t(String));
  data_reg_field_t(g_dataReg, FetchOrigin, authUser, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, FetchOrigin, authPass, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, FetchOrigin, cacheTime, data_prim_t(TimeDuration), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, FetchOrigin, assets, data_prim_t(String), .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, FetchConfig);
  data_reg_field_t(g_dataReg, FetchConfig, outputPath, data_prim_t(String));
  data_reg_field_t(g_dataReg, FetchConfig, origins, t_FetchOrigin, .container = DataContainer_HeapArray);

  data_reg_opaque_t(g_dataReg, NetHttpEtag);

  data_reg_struct_t(g_dataReg, FetchRegistryEntry);
  data_reg_field_t(g_dataReg, FetchRegistryEntry, pathHash, data_prim_t(u32));
  data_reg_field_t(g_dataReg, FetchRegistryEntry, etag, t_NetHttpEtag);
  data_reg_field_t(g_dataReg, FetchRegistryEntry, lastSyncTime, data_prim_t(i64));

  data_reg_struct_t(g_dataReg, FetchRegistry);
  data_reg_field_t(g_dataReg, FetchRegistry, entries, t_FetchRegistryEntry, .container = DataContainer_DynArray);
  // clang-format on

  g_fetchConfigMeta   = data_meta_t(t_FetchConfig);
  g_fetchRegistryMeta = data_meta_t(t_FetchRegistry);
}

static i8 fetch_compare_registry_entry(const void* a, const void* b) {
  const FetchRegistryEntry* entryA = a;
  const FetchRegistryEntry* entryB = b;
  return compare_u32(&entryA->pathHash, &entryB->pathHash);
}

static bool fetch_config_load(const String path, FetchConfig* out) {
  bool       success = false;
  File*      file    = null;
  FileResult fileRes;
  if ((fileRes = file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file))) {
    log_e("Failed to open config file", log_param("err", fmt_text(file_result_str(fileRes))));
    goto Ret;
  }
  String data;
  if ((fileRes = file_map(file, 0 /* offset */, 0 /* size */, FileHints_Prefetch, &data))) {
    log_e("Failed to map config file", log_param("err", fmt_text(file_result_str(fileRes))));
    goto Ret;
  }
  DataReadResult result;
  const Mem      outMem = mem_create(out, sizeof(FetchConfig));
  data_read_json(g_dataReg, data, g_allocHeap, g_fetchConfigMeta, outMem, &result);
  if (result.error) {
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

static u32 fetch_config_max_origin_assets(const FetchConfig* cfg) {
  u32 res = 0;
  heap_array_for_t(cfg->origins, FetchOrigin, origin) {
    res = math_max(res, (u32)origin->assets.count);
  }
  return res;
}

static String fetch_config_out_path_scratch(const FetchConfig* cfg, const String cfgPath) {
  return path_build_scratch(path_parent(cfgPath), cfg->outputPath);
}

static TimeDuration fetch_origin_cache_dur(const FetchOrigin* origin) {
  return origin->cacheTime ? origin->cacheTime : time_day;
}

static NetHttpAuth fetch_origin_auth(const FetchOrigin* origin) {
  if (string_is_empty(origin->authUser)) {
    return (NetHttpAuth){0};
  }
  return (NetHttpAuth){
      .type = NetHttpAuthType_Basic,
      .user = origin->authUser,
      .pw   = origin->authPass,
  };
}

static String fetch_origin_uri_scratch(const FetchOrigin* origin, const String asset) {
  static const String g_separator = string_static("/");

  DynString res = dynstring_create(g_allocScratch, 256);
  if (!string_starts_with(origin->rootUri, g_separator)) {
    dynstring_append(&res, g_separator);
  }
  dynstring_append(&res, origin->rootUri);
  if (!string_ends_with(dynstring_view(&res), g_separator)) {
    dynstring_append(&res, g_separator);
  }
  if (string_starts_with(asset, g_separator)) {
    dynstring_append(&res, string_consume(asset, g_separator.size));
  } else {
    dynstring_append(&res, asset);
  }
  return dynstring_view(&res);
}

static String fetch_registry_path_scratch(const String outputPath) {
  return path_build_scratch(outputPath, string_lit("registry.blob"));
}

static void fetch_registry_load_or_default(const String outputPath, FetchRegistry* out) {
  const String path = fetch_registry_path_scratch(outputPath);
  File*        file = null;
  FileResult   fileRes;
  if ((fileRes = file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file))) {
    goto Default;
  }
  String data;
  if ((fileRes = file_map(file, 0 /* offset */, 0 /* size */, FileHints_Prefetch, &data))) {
    goto Default;
  }
  DataReadResult readRes;
  const Mem      regMem = mem_create(out, sizeof(FetchRegistry));
  data_read_bin(g_dataReg, data, g_allocHeap, g_fetchRegistryMeta, regMem, &readRes);
  if (readRes.error) {
    log_w(
        "Failed to read fetch registry",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(readRes.errorMsg)));
    goto Default;
  }
  goto Ret;

Default:
  *out = (FetchRegistry){
      .entries = dynarray_create_t(g_allocHeap, FetchRegistryEntry, 64),
  };

Ret:
  if (file) {
    file_destroy(file);
  }
}

static void fetch_registry_save(const FetchRegistry* reg, const String outputPath) {
  const String path = fetch_registry_path_scratch(outputPath);

  DynString buffer = dynstring_create(g_allocHeap, 4 * usize_kibibyte);
  const Mem regMem = mem_create(reg, sizeof(FetchRegistry));
  data_write_bin(g_dataReg, &buffer, g_fetchRegistryMeta, regMem);

  const FileResult fileRes = file_write_to_path_atomic(path, dynstring_view(&buffer));
  if (fileRes != FileResult_Success) {
    log_e(
        "Failed to write registry file",
        log_param("path", fmt_path(path)),
        log_param("err", fmt_text(file_result_str(fileRes))));
  }

  dynstring_destroy(&buffer);
}

static void fetch_registry_destroy(FetchRegistry* reg) {
  data_destroy(g_dataReg, g_allocHeap, g_fetchRegistryMeta, mem_create(reg, sizeof(FetchRegistry)));
}

static FetchRegistryEntry* fetch_registry_get(FetchRegistry* reg, const String asset) {
  const FetchRegistryEntry key = {.pathHash = string_hash(asset)};
  return dynarray_search_binary(&reg->entries, fetch_compare_registry_entry, &key);
}

static FetchRegistryEntry* fetch_registry_update(FetchRegistry* reg, const String asset) {
  const FetchRegistryEntry key = {.pathHash = string_hash(asset)};

  FetchRegistryEntry* entry =
      dynarray_find_or_insert_sorted(&reg->entries, fetch_compare_registry_entry, &key);

  entry->pathHash     = key.pathHash;
  entry->lastSyncTime = time_real_clock();
  return entry;
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

static bool fetch_asset_save(
    FetchRegistry*  reg,
    const String    outPath,
    const String    asset,
    NetRest*        rest,
    const NetRestId request) {
  const String path = path_build_scratch(outPath, asset);
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
    return false;
  }

  FetchRegistryEntry* regEntry = fetch_registry_update(reg, asset);
  regEntry->etag               = *net_rest_etag(rest, request);

  log_i(
      "Asset fetched: '{}'",
      log_param("asset", fmt_text(asset)),
      log_param("size", fmt_size(data.size)));
  return true;
}

typedef struct {
  NetRestId id;
  String    asset;
} FetchRequest;

typedef enum {
  FetchResult_Success,
  FetchResult_NetworkUnavailable,
  FetchResult_DownloadFailed,
  FetchResult_SaveFailed,
  FetchResult_Interupted,
} FetchResult;

static String fetch_result_str(const FetchResult result) {
  static const String g_resultNames[] = {
      [FetchResult_Success]            = string_static("Success"),
      [FetchResult_NetworkUnavailable] = string_static("NetworkUnavailable"),
      [FetchResult_DownloadFailed]     = string_static("DownloadFailed"),
      [FetchResult_SaveFailed]         = string_static("SaveFailed"),
      [FetchResult_Interupted]         = string_static("Interupted"),
  };
  return g_resultNames[result];
}

static FetchResult fetch_run_origin(
    const FetchOrigin* origin,
    FetchRegistry*     reg,
    const FetchFlags   flags,
    const String       outPath,
    NetRest*           rest) {
  FetchResult result = FetchResult_Success;

  const TimeReal     now      = time_real_clock();
  const NetHttpAuth  auth     = fetch_origin_auth(origin);
  const TimeDuration cacheDur = fetch_origin_cache_dur(origin);

  DynArray requests = dynarray_create_t(g_allocHeap, FetchRequest, 64);

  // Submit GET requests.
  heap_array_for_t(origin->assets, String, asset) {
    const FileInfo      fileInfo = file_stat_path_sync(path_build_scratch(outPath, *asset));
    FetchRegistryEntry* regEntry = fetch_registry_get(reg, *asset);

    const bool expired = !regEntry || time_real_duration(regEntry->lastSyncTime, now) > cacheDur;
    const bool invalid = fileInfo.type != FileType_Regular;
    if (!expired && !invalid && !(flags & FetchFlags_Force)) {
      continue; // Cache entry still valid; do nothing.
    }
    const NetHttpEtag* etag                   = (regEntry && !invalid) ? &regEntry->etag : null;
    const String       uri                    = fetch_origin_uri_scratch(origin, *asset);
    *dynarray_push_t(&requests, FetchRequest) = (FetchRequest){
        .id    = net_rest_get(rest, origin->host, uri, &auth, etag),
        .asset = *asset,
    };
  }

  // Verify there's a network interface available.
  if (requests.size) {
    NetAddr addrs[32];
    u32     addrCount = array_elems(addrs);
    if (net_interfaces(addrs, &addrCount, NetInterfaceQueryFlags_None) || !addrCount) {
      result = FetchResult_NetworkUnavailable;
    }
  }

  // Process the results.
  while (requests.size && result == FetchResult_Success) {
    thread_sleep(time_milliseconds(100));

    if (signal_is_received(Signal_Interrupt) || signal_is_received(Signal_Terminate)) {
      result = FetchResult_Interupted;
      break;
    }

    for (usize i = requests.size; i-- != 0;) {
      const FetchRequest* req = dynarray_at_t(&requests, i, FetchRequest);
      if (!net_rest_done(rest, req->id)) {
        continue;
      }
      const NetResult reqResult = net_rest_result(rest, req->id);
      switch (reqResult) {
      case NetResult_HttpNotModified:
        fetch_registry_update(reg, req->asset); // Update the lastSyncTime in the registry.
        break;
      case NetResult_Success:
        if (!fetch_asset_save(reg, outPath, req->asset, rest, req->id)) {
          result = FetchResult_SaveFailed;
        }
        break;
      default:
        log_e(
            "Asset fetch failed: '{}'",
            log_param("asset", fmt_text(req->asset)),
            log_param("error", fmt_text(net_result_str(reqResult))));
        result = FetchResult_DownloadFailed;
        break;
      }
      net_rest_release(rest, req->id);
      dynarray_remove_unordered(&requests, i, 1);
    }
  }

  dynarray_destroy(&requests);
  return result;
}

static FetchResult fetch_run(
    const FetchConfig* cfg, FetchRegistry* reg, const FetchFlags flags, const String outPath) {
  FetchResult      result    = FetchResult_Success;
  NetRest*         rest      = null;
  const TimeSteady timeStart = time_steady_clock();

  log_i(
      "Fetching assets",
      log_param("origins", fmt_int(cfg->origins.count)),
      log_param("output-path", fmt_path(outPath)));

  const u32 maxRequests = fetch_config_max_origin_assets(cfg);
  if (maxRequests) {
    rest = net_rest_create(g_allocHeap, fetch_worker_count, maxRequests, fetch_http_flags());

    heap_array_for_t(cfg->origins, FetchOrigin, origin) {
      FetchResult originResult = FetchResult_Success;
      if (origin->assets.count) {
        originResult = fetch_run_origin(origin, reg, flags, outPath, rest);
      }
      result = math_max(result, originResult);

      if (signal_is_received(Signal_Interrupt) || signal_is_received(Signal_Terminate)) {
        result = FetchResult_Interupted;
        break;
      }
    }
  }

  if (rest) {
    net_rest_destroy(rest);
  }

  const TimeDuration duration = time_steady_duration(timeStart, time_steady_clock());
  const NetStats     netStats = net_stats_query();
  if (result == FetchResult_Success) {
    log_i(
        "Fetch finished",
        log_param("duration", fmt_duration(duration)),
        log_param("resolves", fmt_int(netStats.totalResolves)),
        log_param("connects", fmt_int(netStats.totalConnects)),
        log_param("bytes-in", fmt_size(netStats.totalBytesRead)),
        log_param("bytes-out", fmt_size(netStats.totalBytesWrite)));
  } else {
    log_e(
        "Fetch failed",
        log_param("error", fmt_text(fetch_result_str(result))),
        log_param("duration", fmt_duration(duration)),
        log_param("resolves", fmt_int(netStats.totalResolves)),
        log_param("connects", fmt_int(netStats.totalConnects)),
        log_param("bytes-in", fmt_size(netStats.totalBytesRead)),
        log_param("bytes-out", fmt_size(netStats.totalBytesWrite)));
  }
  return result;
}

static bool fetch_is_complete(const FetchConfig* cfg, FetchRegistry* reg, const String outPath) {
  heap_array_for_t(cfg->origins, FetchOrigin, origin) {
    heap_array_for_t(origin->assets, String, asset) {
      const FileInfo fileInfo = file_stat_path_sync(path_build_scratch(outPath, *asset));
      if (fileInfo.type != FileType_Regular) {
        return false; // File is missing.
      }
      FetchRegistryEntry* regEntry = fetch_registry_get(reg, *asset);
      if (!regEntry) {
        return false; // File was never synced with the remote.
      }
      // NOTE: File might be out-of-date but its present.
    }
  }
  return true;
}

static CliId g_optConfigPath, g_optVerbose, g_optForce;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Fetch utility."));

  g_optConfigPath = cli_register_arg(app, string_lit("config"), CliOptionFlags_Required);
  cli_register_desc(app, g_optConfigPath, string_lit("Path to a fetch config file."));
  cli_register_validator(app, g_optConfigPath, cli_validate_file_regular);

  g_optVerbose = cli_register_flag(app, 'v', string_lit("verbose"), CliOptionFlags_None);
  g_optForce   = cli_register_flag(app, 'f', string_lit("force"), CliOptionFlags_None);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {

  const LogMask logMask = cli_parse_provided(invoc, g_optVerbose) ? LogMask_All : ~LogMask_Debug;
  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, logMask));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  fetch_data_init();

  FetchFlags flags = 0;
  if (cli_parse_provided(invoc, g_optForce)) {
    flags |= FetchFlags_Force;
  }

  const String cfgPath = cli_read_string(invoc, g_optConfigPath, string_empty);
  FetchConfig  cfg;
  if (!fetch_config_load(cfgPath, &cfg)) {
    return 1;
  }
  const String outPath = string_dup(g_allocHeap, fetch_config_out_path_scratch(&cfg, cfgPath));

  FetchRegistry reg;
  fetch_registry_load_or_default(outPath, &reg);

  i32 retCode = 0;
  if (file_create_dir_sync(outPath) != FileResult_Success) {
    log_e("Failed to create output directory", log_param("path", fmt_path(outPath)));
    retCode = 1;
    goto Done;
  }

  signal_intercept_enable(); // Custom interrupt handling.

  net_init();
  const FetchResult fetchResult = fetch_run(&cfg, &reg, flags, outPath);
  /**
   * NOTE: If fetch fails but the local registry is complete return 0 to indicate the game can be
   * launched. This makes development while being offline for extended periods nicer.
   */
  if (fetchResult && ((flags & FetchFlags_Force) || !fetch_is_complete(&cfg, &reg, outPath))) {
    retCode = (i32)fetchResult;
  }
  net_teardown();

  fetch_registry_save(&reg, outPath);

Done:
  string_free(g_allocHeap, outPath);
  fetch_registry_destroy(&reg);
  fetch_config_destroy(&cfg);
  return retCode;
}
