#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
#include "core_ascii.h"
#include "core_compare.h"
#include "core_dynbitset.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "core_path.h"
#include "core_search.h"
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
  VKGEN_HASH(alias)                                                                                \
  VKGEN_HASH(api)                                                                                  \
  VKGEN_HASH(basetype)                                                                             \
  VKGEN_HASH(bitmask)                                                                              \
  VKGEN_HASH(bitpos)                                                                               \
  VKGEN_HASH(category)                                                                             \
  VKGEN_HASH(command)                                                                              \
  VKGEN_HASH(commands)                                                                             \
  VKGEN_HASH(comment)                                                                              \
  VKGEN_HASH(constants)                                                                            \
  VKGEN_HASH(define)                                                                               \
  VKGEN_HASH(deprecated)                                                                           \
  VKGEN_HASH(dir)                                                                                  \
  VKGEN_HASH(enum)                                                                                 \
  VKGEN_HASH(enums)                                                                                \
  VKGEN_HASH(extends)                                                                              \
  VKGEN_HASH(extension)                                                                            \
  VKGEN_HASH(extensions)                                                                           \
  VKGEN_HASH(extnumber)                                                                            \
  VKGEN_HASH(feature)                                                                              \
  VKGEN_HASH(funcpointer)                                                                          \
  VKGEN_HASH(handle)                                                                               \
  VKGEN_HASH(include)                                                                              \
  VKGEN_HASH(member)                                                                               \
  VKGEN_HASH(name)                                                                                 \
  VKGEN_HASH(number)                                                                               \
  VKGEN_HASH(offset)                                                                               \
  VKGEN_HASH(param)                                                                                \
  VKGEN_HASH(proto)                                                                                \
  VKGEN_HASH(require)                                                                              \
  VKGEN_HASH(struct)                                                                               \
  VKGEN_HASH(supported)                                                                            \
  VKGEN_HASH(type)                                                                                 \
  VKGEN_HASH(types)                                                                                \
  VKGEN_HASH(union)                                                                                \
  VKGEN_HASH(value)

#define VKGEN_HASH(_NAME_) static StringHash g_hash_##_NAME_;
VKGEN_VISIT_HASHES
#undef VKGEN_HASH

static void vkgen_init_hashes(void) {
#define VKGEN_HASH(_NAME_) g_hash_##_NAME_ = string_hash_lit(#_NAME_);
  VKGEN_VISIT_HASHES
#undef VKGEN_HASH
}

static const String g_vkgenFeatures[] = {
    string_static("VK_VERSION_1_0"),
    string_static("VK_VERSION_1_1"),
};

static const String g_vkgenExtensions[] = {
    string_static("VK_EXT_validation_features"),
    string_static("VK_EXT_debug_utils"),
    string_static("VK_KHR_swapchain"),
    string_static("VK_KHR_surface"),
    string_static("VK_KHR_xcb_surface"),
    string_static("VK_KHR_win32_surface"),
};

static u32 vkgen_feat_find(const StringHash featHash) {
  for (u32 i = 0; i != array_elems(g_vkgenFeatures); ++i) {
    if (string_hash(g_vkgenFeatures[i]) == featHash) {
      return i;
    }
  }
  return sentinel_u32;
}

static u32 vkgen_ext_find(const StringHash extHash) {
  for (u32 i = 0; i != array_elems(g_vkgenExtensions); ++i) {
    if (string_hash(g_vkgenExtensions[i]) == extHash) {
      return i;
    }
  }
  return sentinel_u32;
}

static i64 vkgen_to_int(String str) {
  u8 base = 10;
  if (string_starts_with(str, string_lit("0x"))) {
    str  = string_consume(str, 2);
    base = 16;
  }
  i64 value;
  format_read_i64(str, &value, base);
  return value;
}

static void vkgen_collapse_whitespace(DynString* str) {
  bool inWhitespace = false;
  for (usize i = 0; i != str->size; ++i) {
    const u8 ch = *string_at(dynstring_view(str), i);
    if (ascii_is_whitespace(ch)) {
      if (inWhitespace) {
        dynstring_erase_chars(str, i--, 1);
      }
      inWhitespace = true;
    } else {
      inWhitespace = false;
    }
  }
}

static String vkgen_collapse_whitespace_scratch(const String text) {
  DynString buffer = dynstring_create(g_allocScratch, text.size);
  dynstring_append(&buffer, text);
  vkgen_collapse_whitespace(&buffer);
  return dynstring_view(&buffer);
}

static bool vkgen_str_list_contains(String str, const String other) {
  while (!string_is_empty(str)) {
    usize len = string_find_first_char(str, ',');
    if (sentinel_check(len)) {
      len = str.size;
    }
    const String elem = string_slice(str, 0, len);
    if (string_eq(elem, other)) {
      return true;
    }
    str = string_consume(str, len);
  }
  return false;
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

Ret:
  dynstring_destroy(&buffer);
  net_http_shutdown_sync(http);
  net_http_destroy(http);
  return node;
}

typedef struct {
  String name, value; // Allocated in the schema document.
} VkGenConstant;

typedef enum {
  VkGenTypeKind_None, // Skipped type.
  VkGenTypeKind_Simple,
  VkGenTypeKind_Handle,
  VkGenTypeKind_Enum,
  VkGenTypeKind_Struct,
  VkGenTypeKind_Union,
} VkGenTypeKind;

typedef struct {
  VkGenTypeKind kind;
  StringHash    key;
  String        name; // Allocated in the schema document.
  XmlNode       schemaNode;
} VkGenType;

typedef struct {
  StringHash key;  // Enum this entry is part of.
  String     name; // Allocated in the schema document.
  i64        value;
} VkGenEnumEntry;

typedef struct {
  StringHash key;
  String     name, type; // Allocated in the schema document.
  XmlNode    schemaNode;
} VkGenCommand;

typedef struct {
  XmlDoc*   schemaDoc;
  XmlNode   schemaRoot;
  String    schemaHost, schemaUri;
  DynArray  types; // VkGenType[]
  DynBitSet typesWritten;
  DynArray  constants;   // VkGenConstant[]
  DynArray  enumEntries; // VkGenEnumEntry[]
  DynArray  commands;    // VkGenCommand[]
  DynBitSet commandsWritten;
  XmlNode   featureNodes[array_elems(g_vkgenFeatures)];
  XmlNode   extensionNodes[array_elems(g_vkgenExtensions)];
  DynString out;
} VkGenContext;

static i8 vkgen_compare_command(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, VkGenCommand, key), field_ptr(b, VkGenCommand, key));
}

static i8 vkgen_compare_type(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, VkGenType, key), field_ptr(b, VkGenType, key));
}

static i8 vkgen_compare_enum_entry(const void* a, const void* b) {
  const VkGenEnumEntry* entryA = a;
  const VkGenEnumEntry* entryB = b;
  const i8              order  = compare_stringhash(&entryA->key, &entryB->key);
  return order ? order : compare_i64(&entryA->value, &entryB->value);
}

static i8 vkgen_compare_enum_entry_no_value(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, VkGenEnumEntry, key), field_ptr(b, VkGenEnumEntry, key));
}

static u32 vkgen_type_find(VkGenContext* ctx, const StringHash key) {
  const VkGenType tgt = {.key = key};
  VkGenType*      res = dynarray_search_binary(&ctx->types, vkgen_compare_type, &tgt);
  return res ? (u32)(res - dynarray_begin_t(&ctx->types, VkGenType)) : sentinel_u32;
}

static const VkGenType* vkgen_type_get(VkGenContext* ctx, const u32 index) {
  return dynarray_at_t(&ctx->types, index, VkGenType);
}

static void vkgen_type_push(
    VkGenContext* ctx, const VkGenTypeKind kind, const String name, const XmlNode schemaNode) {
  *dynarray_push_t(&ctx->types, VkGenType) = (VkGenType){
      .kind       = kind,
      .key        = string_hash(name),
      .name       = name,
      .schemaNode = schemaNode,
  };
}

static bool vkgen_enum_entry_push(VkGenContext* ctx, const VkGenEnumEntry enumEntry) {
  VkGenEnumEntry* stored =
      dynarray_find_or_insert_sorted(&ctx->enumEntries, vkgen_compare_enum_entry, &enumEntry);

  if (stored->key) {
    return false; // Duplicate.
  }
  *stored = enumEntry;
  return true;
}

typedef struct {
  const VkGenEnumEntry* begin;
  const VkGenEnumEntry* end;
} VkGenEnumEntries;

static VkGenEnumEntries vkgen_enum_entries_find(VkGenContext* ctx, const StringHash enumKey) {
  const VkGenEnumEntry* entriesBegin = dynarray_begin_t(&ctx->enumEntries, VkGenEnumEntry);
  const VkGenEnumEntry* entriesEnd   = dynarray_end_t(&ctx->enumEntries, VkGenEnumEntry);

  VkGenEnumEntry        tgt = {.key = enumKey};
  const VkGenEnumEntry* gt  = search_binary_greater_t(
      entriesBegin, entriesEnd, VkGenEnumEntry, vkgen_compare_enum_entry_no_value, &tgt);

  VkGenEnumEntries res = {.begin = gt ? gt : entriesEnd, .end = gt ? gt : entriesEnd};
  if (res.begin == entriesBegin) {
    return (VkGenEnumEntries){0};
  }

  // Find the beginning of the entries for this enum.
  for (; res.begin != entriesBegin && (res.begin - 1)->key == enumKey; --res.begin)
    ;

  return res;
}

static u32 vkgen_command_find(VkGenContext* ctx, const StringHash key) {
  const VkGenCommand tgt = {.key = key};
  VkGenCommand*      res = dynarray_search_binary(&ctx->commands, vkgen_compare_command, &tgt);
  return res ? (u32)(res - dynarray_begin_t(&ctx->commands, VkGenCommand)) : sentinel_u32;
}

static const VkGenCommand* vkgen_command_get(VkGenContext* ctx, const u32 index) {
  return dynarray_at_t(&ctx->commands, index, VkGenCommand);
}

static void
vkgen_command_push(VkGenContext* ctx, const String name, const String type, const XmlNode node) {
  *dynarray_push_t(&ctx->commands, VkGenCommand) = (VkGenCommand){
      .key        = string_hash(name),
      .name       = name,
      .type       = type,
      .schemaNode = node,
  };
}

static bool vkgen_is_supported(VkGenContext* ctx, const XmlNode node) {
  const String apis = xml_attr_get(ctx->schemaDoc, node, g_hash_supported);
  return string_is_empty(apis) || vkgen_str_list_contains(apis, string_lit("vulkan"));
}

static bool vkgen_is_supported_api(VkGenContext* ctx, const XmlNode node) {
  const String apis = xml_attr_get(ctx->schemaDoc, node, g_hash_api);
  return string_is_empty(apis) || vkgen_str_list_contains(apis, string_lit("vulkan"));
}

static bool vkgen_is_deprecated(VkGenContext* ctx, const XmlNode node) {
  return !string_is_empty(xml_attr_get(ctx->schemaDoc, node, g_hash_deprecated));
}

static VkGenTypeKind vkgen_categorize_type(VkGenContext* ctx, const XmlNode typeNode) {
  const StringHash catHash = xml_attr_get_hash(ctx->schemaDoc, typeNode, g_hash_category);
  if (catHash == g_hash_basetype || catHash == g_hash_bitmask || catHash == g_hash_funcpointer) {
    return VkGenTypeKind_Simple;
  }
  if (catHash == g_hash_handle) {
    return VkGenTypeKind_Handle;
  }
  if (catHash == g_hash_enum) {
    return VkGenTypeKind_Enum;
  }
  if (catHash == g_hash_struct) {
    return VkGenTypeKind_Struct;
  }
  if (catHash == g_hash_union) {
    return VkGenTypeKind_Union;
  }
  return VkGenTypeKind_None;
}

static void vkgen_collect_constants(VkGenContext* ctx) {
  xml_for_children(ctx->schemaDoc, ctx->schemaRoot, enumNode) {
    if (xml_name_hash(ctx->schemaDoc, enumNode) != g_hash_enums) {
      continue; // Not an enum.
    }
    const StringHash typeHash = xml_attr_get_hash(ctx->schemaDoc, enumNode, g_hash_type);
    if (typeHash != g_hash_constants) {
      continue; // Not constants.
    }
    if (!vkgen_is_supported_api(ctx, enumNode)) {
      continue; // Not supported.
    }
    xml_for_children(ctx->schemaDoc, enumNode, entryNode) {
      if (xml_name_hash(ctx->schemaDoc, entryNode) != g_hash_enum) {
        continue; // Not an enum entry.
      }
      if (vkgen_is_deprecated(ctx, entryNode)) {
        continue; // Is deprecated.
      }
      *dynarray_push_t(&ctx->constants, VkGenConstant) = (VkGenConstant){
          .name  = xml_attr_get(ctx->schemaDoc, entryNode, g_hash_name),
          .value = xml_attr_get(ctx->schemaDoc, entryNode, g_hash_value),
      };
    }
  }
  log_i("Collected constants", log_param("count", fmt_int(ctx->constants.size)));
}

static void vkgen_collect_enums(VkGenContext* ctx) {
  u32 enumCount = 0;
  xml_for_children(ctx->schemaDoc, ctx->schemaRoot, enumNode) {
    if (xml_name_hash(ctx->schemaDoc, enumNode) != g_hash_enums) {
      continue; // Not an enum.
    }
    const StringHash typeHash = xml_attr_get_hash(ctx->schemaDoc, enumNode, g_hash_type);
    if (typeHash != g_hash_enum && typeHash != g_hash_bitmask) {
      continue; // Not an absolute / bitmask enum (could be constants).
    }
    if (!vkgen_is_supported_api(ctx, enumNode)) {
      continue; // Not supported.
    }
    const String name = xml_attr_get(ctx->schemaDoc, enumNode, g_hash_name);
    vkgen_type_push(ctx, VkGenTypeKind_Enum, name, enumNode);
    ++enumCount;

    xml_for_children(ctx->schemaDoc, enumNode, entryNode) {
      if (xml_name_hash(ctx->schemaDoc, entryNode) != g_hash_enum) {
        continue; // Not an enum entry.
      }
      if (vkgen_is_deprecated(ctx, entryNode)) {
        continue; // Is deprecated.
      }
      if (xml_attr_has(ctx->schemaDoc, entryNode, g_hash_alias)) {
        continue; // Aliases are not supported.
      }
      VkGenEnumEntry entryRes;
      entryRes.key  = string_hash(name);
      entryRes.name = xml_attr_get(ctx->schemaDoc, entryNode, g_hash_name);

      const String bitPos = xml_attr_get(ctx->schemaDoc, entryNode, g_hash_bitpos);
      if (!string_is_empty(bitPos)) {
        entryRes.value = u64_lit(1) << vkgen_to_int(bitPos);
      } else {
        entryRes.value = vkgen_to_int(xml_attr_get(ctx->schemaDoc, entryNode, g_hash_value));
      }
      vkgen_enum_entry_push(ctx, entryRes);
    }
  }
  log_i(
      "Collected enums",
      log_param("count", fmt_int(enumCount)),
      log_param("entries", fmt_int(ctx->enumEntries.size)));
}

static void vkgen_collect_enum_extensions(VkGenContext* ctx, const XmlNode node, i64 extNumber) {
  xml_for_children(ctx->schemaDoc, node, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_require) {
      continue; // Not a require element.
    }
    xml_for_children(ctx->schemaDoc, child, entry) {
      const StringHash entryNameHash = xml_name_hash(ctx->schemaDoc, entry);
      if (entryNameHash != g_hash_enum) {
        continue; // Not an enum.
      }
      const StringHash enumKey = xml_attr_get_hash(ctx->schemaDoc, entry, g_hash_extends);
      const String     name    = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
      if (!enumKey || string_is_empty(name)) {
        continue; // Enum or name missing.
      }
      const String bitPosStr = xml_attr_get(ctx->schemaDoc, entry, g_hash_bitpos);
      if (!string_is_empty(bitPosStr)) {
        const VkGenEnumEntry entryRes = {
            .key   = enumKey,
            .name  = name,
            .value = u64_lit(1) << vkgen_to_int(bitPosStr),
        };
        vkgen_enum_entry_push(ctx, entryRes);
        continue;
      }
      const bool   invert   = xml_attr_has(ctx->schemaDoc, entry, g_hash_dir);
      const String valueStr = xml_attr_get(ctx->schemaDoc, entry, g_hash_value);
      if (!string_is_empty(valueStr)) {
        const VkGenEnumEntry entryRes = {
            .key   = enumKey,
            .name  = name,
            .value = vkgen_to_int(valueStr) * (invert ? -1 : 1),
        };
        vkgen_enum_entry_push(ctx, entryRes);
        continue;
      }
      const String offsetStr = xml_attr_get(ctx->schemaDoc, entry, g_hash_offset);
      if (!string_is_empty(offsetStr)) {
        const String extnumStr = xml_attr_get(ctx->schemaDoc, entry, g_hash_extnumber);
        if (!string_is_empty(extnumStr)) {
          extNumber = vkgen_to_int(extnumStr);
        }
        if (extNumber < 0) {
          log_w("Missing extension number");
          continue;
        }
        const i64            value = 1000000000 + (extNumber - 1) * 1000 + vkgen_to_int(offsetStr);
        const VkGenEnumEntry entryRes = {
            .key   = enumKey,
            .name  = name,
            .value = value * (invert ? -1 : 1),
        };
        vkgen_enum_entry_push(ctx, entryRes);
        continue;
      }
    }
  }
}

static void vkgen_collect_types(VkGenContext* ctx) {
  const XmlNode typesNode = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_types);
  xml_for_children(ctx->schemaDoc, typesNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_type) {
      continue; // Not a type.
    }
    if (!vkgen_is_supported_api(ctx, child)) {
      continue; // Not supported.
    }
    const VkGenTypeKind kind = vkgen_categorize_type(ctx, child);
    const String        name = xml_attr_get(ctx->schemaDoc, child, g_hash_name);
    if (!string_is_empty(name)) {
      vkgen_type_push(ctx, kind, name, child);
      continue;
    }
    const XmlNode nameNode = xml_child_get(ctx->schemaDoc, child, g_hash_name);
    if (!sentinel_check(nameNode)) {
      vkgen_type_push(ctx, kind, xml_value(ctx->schemaDoc, nameNode), child);
      continue;
    }
  }
  dynarray_sort(&ctx->types, vkgen_compare_type);
  log_i("Collected types", log_param("count", fmt_int(ctx->types.size)));
}

static void vkgen_collect_commands(VkGenContext* ctx) {
  const XmlNode commandsNode = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_commands);
  xml_for_children(ctx->schemaDoc, commandsNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_command) {
      continue; // Not a command.
    }
    if (!vkgen_is_supported_api(ctx, child)) {
      continue; // Not supported.
    }
    const XmlNode protoNode = xml_child_get(ctx->schemaDoc, child, g_hash_proto);
    if (sentinel_check(protoNode)) {
      continue; // Command without a proto (we don't support aliases).
    }
    const XmlNode protoNameNode = xml_child_get(ctx->schemaDoc, protoNode, g_hash_name);
    const XmlNode protoTypeNode = xml_child_get(ctx->schemaDoc, protoNode, g_hash_type);
    vkgen_command_push(
        ctx,
        xml_value(ctx->schemaDoc, protoNameNode),
        xml_value(ctx->schemaDoc, protoTypeNode),
        child);
  }
  dynarray_sort(&ctx->commands, vkgen_compare_command);
  log_i("Collected commands", log_param("count", fmt_int(ctx->commands.size)));
}

static void vkgen_collect_features(VkGenContext* ctx) {
  mem_set(array_mem(ctx->featureNodes), 0xFF);

  xml_for_children(ctx->schemaDoc, ctx->schemaRoot, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_feature) {
      continue; // Not a feature.
    }
    if (!vkgen_is_supported_api(ctx, child)) {
      continue; // Not supported.
    }
    const StringHash nameHash  = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
    const u32        featIndex = vkgen_feat_find(nameHash);
    if (sentinel_check(featIndex)) {
      continue; // Not an supported extension.
    }
    vkgen_collect_enum_extensions(ctx, child, -1);
    ctx->featureNodes[featIndex] = child;
  }
  log_i("Collected features");
}

static void vkgen_collect_extensions(VkGenContext* ctx) {
  mem_set(array_mem(ctx->extensionNodes), 0xFF);

  const XmlNode extensionsNode = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_extensions);
  xml_for_children(ctx->schemaDoc, extensionsNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_extension) {
      continue; // Not an extension.
    }
    if (!vkgen_is_supported(ctx, child)) {
      continue; // Not supported.
    }
    const StringHash nameHash = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
    const u32        extIndex = vkgen_ext_find(nameHash);
    if (sentinel_check(extIndex)) {
      continue; // Not an supported extension.
    }
    const String numberStr = xml_attr_get(ctx->schemaDoc, child, g_hash_number);
    if (string_is_empty(numberStr)) {
      continue;
    }
    vkgen_collect_enum_extensions(ctx, child, vkgen_to_int(numberStr));
    ctx->extensionNodes[extIndex] = child;
  }
  log_i("Collected extensions");
}

static String vkgen_type_resolve(VkGenContext* ctx, XmlNode* node) {
  const String text = xml_value(ctx->schemaDoc, *node);

  const XmlNode next      = xml_next(ctx->schemaDoc, *node);
  const String  nextText  = string_trim_whitespace(xml_value(ctx->schemaDoc, next));
  const bool    isPointer = string_eq(nextText, string_lit("*"));

  if (string_eq(text, string_lit("uint8_t"))) {
    return string_lit("u8");
  }
  if (string_eq(text, string_lit("int32_t"))) {
    return string_lit("i32");
  }
  if (string_eq(text, string_lit("uint32_t"))) {
    return string_lit("u32");
  }
  if (string_eq(text, string_lit("int64_t"))) {
    return string_lit("i64");
  }
  if (string_eq(text, string_lit("uint64_t"))) {
    return string_lit("u64");
  }
  if (string_eq(text, string_lit("size_t"))) {
    return string_lit("usize");
  }
  if (string_eq(text, string_lit("float"))) {
    return string_lit("f32");
  }
  if (string_eq(text, string_lit("double"))) {
    return string_lit("f64");
  }
  if (string_eq(text, string_lit("HINSTANCE"))) {
    return string_lit("uptr");
  }
  if (string_eq(text, string_lit("HWND"))) {
    return string_lit("uptr");
  }
  if (string_eq(text, string_lit("xcb_visualid_t"))) {
    return string_lit("u32");
  }
  if (string_eq(text, string_lit("xcb_window_t"))) {
    return string_lit("uptr");
  }
  if (string_eq(text, string_lit("VK_DEFINE_NON_DISPATCHABLE_HANDLE"))) {
    return string_lit("VK_DEFINE_HANDLE");
  }
  if (string_eq(text, string_lit("xcb_connection_t")) && isPointer) {
    *node = next; // Skip the pointer.
    return string_lit("uptr");
  }
  return text;
}

static String vkgen_text_resolve_scratch(VkGenContext* ctx, const String text) {
  (void)ctx;
  DynString buffer = dynstring_create(g_allocScratch, text.size);
  dynstring_append(&buffer, text);
  vkgen_collapse_whitespace(&buffer);
  dynstring_replace(&buffer, string_lit("VKAPI_PTR"), string_lit("SYS_DECL"));
  return dynstring_view(&buffer);
}

static void vkgen_write_node(VkGenContext* ctx, const XmlNode node) {
  bool lastIsElement = false;
  xml_for_children(ctx->schemaDoc, node, part) {
    switch (xml_type(ctx->schemaDoc, part)) {
    case XmlType_Element: {
      const StringHash nameHash = xml_name_hash(ctx->schemaDoc, part);
      if (nameHash == g_hash_comment) {
        continue;
      }
      String text;
      if (nameHash == g_hash_type) {
        text = vkgen_type_resolve(ctx, &part);
      } else {
        text = xml_value(ctx->schemaDoc, part);
      }
      if (lastIsElement) {
        fmt_write(&ctx->out, " ");
      }
      lastIsElement = true;
      fmt_write(&ctx->out, "{}", fmt_text(text, .flags = FormatTextFlags_SingleLine));
    } break;
    case XmlType_Text: {
      const String str = vkgen_text_resolve_scratch(ctx, xml_value(ctx->schemaDoc, part));
      fmt_write(&ctx->out, "{}", fmt_text(str, .flags = FormatTextFlags_SingleLine));
      lastIsElement = false;
    } break;
    default:
      break;
    }
  }
}

static void vkgen_write_type_enum(VkGenContext* ctx, const VkGenType* type) {
  VkGenEnumEntries entries = vkgen_enum_entries_find(ctx, type->key);
  if (entries.begin == entries.end) {
    return; // Empty enum;
  }
  fmt_write(&ctx->out, "typedef enum {\n");
  for (const VkGenEnumEntry* itr = entries.begin; itr != entries.end; ++itr) {
    fmt_write(&ctx->out, "  {} = {},\n", fmt_text(itr->name), fmt_int(itr->value));
  }
  fmt_write(&ctx->out, "} {};\n\n", fmt_text(type->name));
}

static void vkgen_write_type_struct(VkGenContext* ctx, const VkGenType* type) {
  if (sentinel_check(xml_first_child(ctx->schemaDoc, type->schemaNode))) {
    return; // Empty struct.
  }
  fmt_write(&ctx->out, "typedef struct {} {\n", fmt_text(type->name));

  xml_for_children(ctx->schemaDoc, type->schemaNode, entry) {
    const StringHash nameHash = xml_name_hash(ctx->schemaDoc, entry);
    if (nameHash != g_hash_member || !vkgen_is_supported_api(ctx, entry)) {
      continue; // Not a (supported) struct member.
    }
    fmt_write(&ctx->out, "  ");
    vkgen_write_node(ctx, entry);
    fmt_write(&ctx->out, ";\n");
  }
  fmt_write(&ctx->out, "} {};\n\n", fmt_text(type->name));
}

static void vkgen_write_type_union(VkGenContext* ctx, const VkGenType* type) {
  fmt_write(&ctx->out, "typedef union {} {\n", fmt_text(type->name));

  xml_for_children(ctx->schemaDoc, type->schemaNode, entry) {
    const StringHash nameHash = xml_name_hash(ctx->schemaDoc, entry);
    if (nameHash != g_hash_member || !vkgen_is_supported_api(ctx, entry)) {
      continue; // Not a (supported) union member.
    }
    fmt_write(&ctx->out, "  ");
    vkgen_write_node(ctx, entry);
    fmt_write(&ctx->out, ";\n");
  }
  fmt_write(&ctx->out, "} {};\n\n", fmt_text(type->name));
}

static bool vkgen_write_type(VkGenContext*, StringHash key);

static bool vkgen_write_dependencies(VkGenContext* ctx, const XmlNode typeNode) {
  bool success = true;
  xml_for_children(ctx->schemaDoc, typeNode, entry) {
    if (xml_type(ctx->schemaDoc, entry) != XmlType_Element) {
      continue; // Not an element.
    }
    if (xml_name_hash(ctx->schemaDoc, entry) == g_hash_type) {
      const String type = xml_value(ctx->schemaDoc, entry);
      success &= vkgen_write_type(ctx, string_hash(type));
      continue;
    }
    success &= vkgen_write_dependencies(ctx, entry);
  }
  return success;
}

static bool vkgen_write_type(VkGenContext* ctx, const StringHash key) {
  const u32 typeIndex = vkgen_type_find(ctx, key);
  if (sentinel_check(typeIndex)) {
    return false; // Unknown type.
  }
  if (dynbitset_test(&ctx->typesWritten, typeIndex)) {
    return true; // Already written.
  }
  dynbitset_set(&ctx->typesWritten, typeIndex);

  // Write types we depend on.
  const VkGenType* type = vkgen_type_get(ctx, typeIndex);
  if (!vkgen_write_dependencies(ctx, type->schemaNode)) {
    return false;
  }
  const String name = type->name;

  // Write type definition.
  switch (type->kind) {
  case VkGenTypeKind_None:
    break;
  case VkGenTypeKind_Simple:
    vkgen_write_node(ctx, type->schemaNode);
    fmt_write(&ctx->out, "\n\n");
    break;
  case VkGenTypeKind_Handle:
    fmt_write(&ctx->out, "typedef struct {}_T* {};\n\n", fmt_text(name), fmt_text(name));
    break;
  case VkGenTypeKind_Enum:
    vkgen_write_type_enum(ctx, type);
    break;
  case VkGenTypeKind_Struct:
    vkgen_write_type_struct(ctx, type);
    break;
  case VkGenTypeKind_Union:
    vkgen_write_type_union(ctx, type);
    break;
  }
  return true;
}

static void vkgen_write_command_params(VkGenContext* ctx, const XmlNode cmdNode) {
  bool anyParam = false;
  xml_for_children(ctx->schemaDoc, cmdNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_param) {
      continue; // Not a parameter.
    }
    if (!vkgen_is_supported_api(ctx, child)) {
      continue; // Not supported.
    }
    if (anyParam) {
      fmt_write(&ctx->out, ", ");
    }
    vkgen_write_node(ctx, child);
    anyParam = true;
  }
  if (!anyParam) {
    fmt_write(&ctx->out, "void");
  }
}

static bool vkgen_write_command(VkGenContext* ctx, const StringHash key) {
  const u32 cmdIndex = vkgen_command_find(ctx, key);
  if (sentinel_check(cmdIndex)) {
    return false; // Unknown command.
  }
  if (dynbitset_test(&ctx->commandsWritten, cmdIndex)) {
    return true; // Already written.
  }
  dynbitset_set(&ctx->commandsWritten, cmdIndex);

  // Write types this command depends on.
  const VkGenCommand* cmd = vkgen_command_get(ctx, cmdIndex);
  if (!vkgen_write_dependencies(ctx, cmd->schemaNode)) {
    return false;
  }

  // Write function pointer type-def.
  fmt_write(&ctx->out, "typedef {} (SYS_DECL *PFN_{})(", fmt_text(cmd->type), fmt_text(cmd->name));
  vkgen_write_command_params(ctx, cmd->schemaNode);
  fmt_write(&ctx->out, ");\n");

  // Write function declaration.
  fmt_write(&ctx->out, "{} SYS_DECL {}(", fmt_text(cmd->type), fmt_text(cmd->name));
  vkgen_write_command_params(ctx, cmd->schemaNode);
  fmt_write(&ctx->out, ");\n\n");

  return true;
}

static bool vkgen_write_requirements(VkGenContext* ctx, const XmlNode node) {
  bool success = true;
  xml_for_children(ctx->schemaDoc, node, set) {
    if (xml_name_hash(ctx->schemaDoc, set) != g_hash_require) {
      continue; // Not a require element.
    }
    xml_for_children(ctx->schemaDoc, set, entry) {
      const StringHash entryNameHash = xml_name_hash(ctx->schemaDoc, entry);
      if (entryNameHash == g_hash_type) {
        if (!vkgen_write_type(ctx, xml_attr_get_hash(ctx->schemaDoc, entry, g_hash_name))) {
          const String typeNameStr = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
          log_e("Failed to write type", log_param("name", fmt_text(typeNameStr)));
          success = false;
        }
        continue;
      }
      if (entryNameHash == g_hash_command) {
        if (!vkgen_write_command(ctx, xml_attr_get_hash(ctx->schemaDoc, entry, g_hash_name))) {
          const String commandNameStr = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
          log_e("Failed to write command", log_param("name", fmt_text(commandNameStr)));
          success = false;
        }
        continue;
      }
    }
  }
  return success;
}

static bool vkgen_write_header(VkGenContext* ctx) {
  fmt_write(&ctx->out, "#pragma once\n");
  fmt_write(
      &ctx->out,
      "// Generated by '{}' from '{}{}'.\n",
      fmt_text(path_stem(g_pathExecutable)),
      fmt_text(ctx->schemaHost),
      fmt_text(ctx->schemaUri));

  const XmlNode copyrightElem = xml_first_child(ctx->schemaDoc, ctx->schemaRoot);
  if (xml_is(ctx->schemaDoc, copyrightElem, XmlType_Element)) {
    const String copyrightText = xml_value(ctx->schemaDoc, copyrightElem);
    const String textTrimmed   = vkgen_collapse_whitespace_scratch(copyrightText);
    fmt_write(&ctx->out, "//{}.\n", fmt_text(textTrimmed, .flags = FormatTextFlags_SingleLine));
  }

  fmt_write(&ctx->out, "// clang-format off\n\n");
  fmt_write(&ctx->out, "#include \"core.h\"\n\n");
  fmt_write(
      &ctx->out,
      "#define VK_MAKE_API_VERSION(variant, major, minor, patch)"
      " ((((u32)(variant)) << 29) |"
      " (((u32)(major)) << 22) |"
      " (((u32)(minor)) << 12) |"
      " ((u32)(patch)))\n\n");

  // Write constants.
  dynarray_for_t(&ctx->constants, VkGenConstant, constant) {
    fmt_write(&ctx->out, "#define {} {}\n", fmt_text(constant->name), fmt_text(constant->value));
  }
  fmt_write(&ctx->out, "\n");

  // Write feature requirements (types and commands).
  for (u32 i = 0; i != array_elems(g_vkgenFeatures); ++i) {
    if (sentinel_check(ctx->featureNodes[i])) {
      return false; // Feature not found.
    }
    if (!vkgen_write_requirements(ctx, ctx->featureNodes[i])) {
      return false; // Feature requirement missing.
    }
  }

  // Write extension requirements (types and commands).
  for (u32 i = 0; i != array_elems(g_vkgenExtensions); ++i) {
    if (sentinel_check(ctx->extensionNodes[i])) {
      return false; // Extension not found.
    }
    if (!vkgen_write_requirements(ctx, ctx->extensionNodes[i])) {
      return false; // Extension requirement missing.
    }
  }

  fmt_write(&ctx->out, "// clang-format on\n");
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
      .schemaDoc       = xml_create(g_allocHeap, 128 * 1024),
      .schemaHost      = cli_read_string(invoc, g_optSchemaHost, g_schemaDefaultHost),
      .schemaUri       = cli_read_string(invoc, g_optSchemaUri, g_schemaDefaultUri),
      .types           = dynarray_create_t(g_allocHeap, VkGenType, 4096),
      .typesWritten    = dynbitset_create(g_allocHeap, 4096),
      .constants       = dynarray_create_t(g_allocHeap, VkGenConstant, 64),
      .enumEntries     = dynarray_create_t(g_allocHeap, VkGenEnumEntry, 2048),
      .commands        = dynarray_create_t(g_allocHeap, VkGenCommand, 1024),
      .commandsWritten = dynbitset_create(g_allocHeap, 1024),
      .out             = dynstring_create(g_allocHeap, usize_kibibyte * 16),
  };

  ctx.schemaRoot = vkgen_schema_get(ctx.schemaDoc, ctx.schemaHost, ctx.schemaUri);
  if (sentinel_check(ctx.schemaRoot)) {
    goto Exit;
  }

  vkgen_collect_constants(&ctx);
  vkgen_collect_enums(&ctx);
  vkgen_collect_types(&ctx);
  vkgen_collect_commands(&ctx);
  vkgen_collect_extensions(&ctx);
  vkgen_collect_features(&ctx);

  if (vkgen_write_header(&ctx)) {
    success = true;
  } else {
    log_e("Failed to write header");
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
  dynbitset_destroy(&ctx.typesWritten);
  dynarray_destroy(&ctx.constants);
  dynarray_destroy(&ctx.enumEntries);
  dynarray_destroy(&ctx.commands);
  dynbitset_destroy(&ctx.commandsWritten);
  dynstring_destroy(&ctx.out);

  net_teardown();
  return success ? 0 : 1;
}
