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
 * VulkanGen - Utility to generate a Vulkan api header and utility c file.
 */

#define VKGEN_VISIT_HASHES                                                                         \
  VKGEN_HASH(alias)                                                                                \
  VKGEN_HASH(api)                                                                                  \
  VKGEN_HASH(basetype)                                                                             \
  VKGEN_HASH(bitmask)                                                                              \
  VKGEN_HASH(bitpos)                                                                               \
  VKGEN_HASH(blockSize)                                                                            \
  VKGEN_HASH(category)                                                                             \
  VKGEN_HASH(command)                                                                              \
  VKGEN_HASH(commands)                                                                             \
  VKGEN_HASH(comment)                                                                              \
  VKGEN_HASH(component)                                                                            \
  VKGEN_HASH(compressed)                                                                           \
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
  VKGEN_HASH(format)                                                                               \
  VKGEN_HASH(formats)                                                                              \
  VKGEN_HASH(funcpointer)                                                                          \
  VKGEN_HASH(handle)                                                                               \
  VKGEN_HASH(include)                                                                              \
  VKGEN_HASH(instance)                                                                             \
  VKGEN_HASH(member)                                                                               \
  VKGEN_HASH(name)                                                                                 \
  VKGEN_HASH(number)                                                                               \
  VKGEN_HASH(offset)                                                                               \
  VKGEN_HASH(param)                                                                                \
  VKGEN_HASH(parent)                                                                               \
  VKGEN_HASH(proto)                                                                                \
  VKGEN_HASH(require)                                                                              \
  VKGEN_HASH(struct)                                                                               \
  VKGEN_HASH(supported)                                                                            \
  VKGEN_HASH(texelsPerBlock)                                                                       \
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

typedef enum {
  VkGenRef_Const   = 1 << 0,
  VkGenRef_Pointer = 1 << 1,
} VkGenRefFlags;

typedef struct {
  String        name;
  VkGenRefFlags flags;
} VkGenRef;

typedef struct {
  String original, replacement;
  bool   stripPointer;
} VkGenRefAlias;

// clang-format off
static const VkGenRefAlias g_vkgenRefAliases[] = {
  {string_static("uint8_t"),            string_static("u8")                         },
  {string_static("int32_t"),            string_static("i32")                        },
  {string_static("uint32_t"),           string_static("u32")                        },
  {string_static("int64_t"),            string_static("i64")                        },
  {string_static("uint64_t"),           string_static("u64")                        },
  {string_static("size_t"),             string_static("usize")                      },
  {string_static("float"),              string_static("f32")                        },
  {string_static("double"),             string_static("f64")                        },
  {string_static("HWND"),               string_static("uptr")                       },
  {string_static("HINSTANCE"),          string_static("uptr")                       },
  {string_static("xcb_visualid_t"),     string_static("u32")                        },
  {string_static("xcb_window_t"),       string_static("uptr")                       },
  {string_static("xcb_connection_t"),   string_static("uptr"), .stripPointer = true },
};
// clang-format on

typedef struct {
  String typeName, entryPrefix;
} VkGenStringify;

static const VkGenStringify g_vkgenStringify[] = {
    {string_static("VkResult"), string_static("VK_")},
    {string_static("VkPhysicalDeviceType"), string_static("VK_PHYSICAL_DEVICE_TYPE_")},
    {string_static("VkColorSpaceKHR"), string_static("VK_COLOR_SPACE_")},
    {string_static("VkPresentModeKHR"), string_static("VK_PRESENT_MODE_")},
    {string_static("VkVendorId"), string_static("VK_VENDOR_ID_")},
    {string_static("VkFormat"), string_static("VK_FORMAT_")},
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

static bool vkgen_node_value_match(XmlDoc* doc, const XmlNode node, const String text) {
  const String nodeText = xml_value(doc, node);
  return string_eq(string_trim_whitespace(nodeText), text);
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
  VkGenTypeKind_FuncPointer,
  VkGenTypeKind_Handle,
  VkGenTypeKind_Enum,
  VkGenTypeKind_Struct,
  VkGenTypeKind_Union,
} VkGenTypeKind;

typedef struct {
  VkGenTypeKind kind;
  StringHash    key;
  StringHash    parent; // Optional, unset if 0.
  XmlNode       schemaNode;
  String        name; // Allocated in the schema document.
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
  StringHash nameHash;
  u32        size;  // Size in bytes of a single pixel.
  u32        comps; // Number of components (aka channels).
  bool       compressed4x4;
} VkGenFormat;

typedef enum {
  VkGenInterfaceCat_loader,
  VkGenInterfaceCat_Instance,
  VkGenInterfaceCat_Device,

  VkGenInterfaceCat_Count,
} VkGenInterfaceCat;

typedef struct {
  VkGenInterfaceCat cat;
  u32               cmdIndex;
} VkGenInterface;

typedef struct {
  XmlDoc*   schemaDoc;
  XmlNode   schemaRoot;
  String    schemaHost, schemaUri;
  DynArray  types; // VkGenType[]
  DynBitSet typesWritten;
  DynArray  constants;   // VkGenConstant[]
  DynArray  enumEntries; // VkGenEnumEntry[]
  DynArray  commands;    // VkGenCommand[]
  XmlNode   featureNodes[array_elems(g_vkgenFeatures)];
  XmlNode   extensionNodes[array_elems(g_vkgenExtensions)];
  DynArray  interfaces; // VkGenInterface[]
  DynArray  formats;    // VkGenFormat[]
  String    outName;
  DynString out;
} VkGenContext;

static u8 vkgen_out_last_char(VkGenContext* ctx) {
  const String text = dynstring_view(&ctx->out);
  return string_is_empty(text) ? '\0' : *string_last(text);
}

static bool vkgen_out_last_is_separator(VkGenContext* ctx) {
  const u8 lastChar = vkgen_out_last_char(ctx);
  return ascii_is_whitespace(lastChar) || lastChar == '(';
}

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

static i8 vkgen_compare_format(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, VkGenFormat, nameHash), field_ptr(b, VkGenFormat, nameHash));
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
    VkGenContext*       ctx,
    const VkGenTypeKind kind,
    const String        name,
    const StringHash    parent,
    const XmlNode       schemaNode) {
  *dynarray_push_t(&ctx->types, VkGenType) = (VkGenType){
      .kind       = kind,
      .key        = string_hash(name),
      .parent     = parent,
      .schemaNode = schemaNode,
      .name       = name,
  };
}

static bool vkgen_is_child(VkGenContext* ctx, const StringHash child, const StringHash parent) {
  if (child == parent) {
    return true;
  }
  const u32 childTypeIndex = vkgen_type_find(ctx, child);
  if (sentinel_check(childTypeIndex)) {
    return false;
  }
  const VkGenType* childType = vkgen_type_get(ctx, childTypeIndex);
  return vkgen_is_child(ctx, childType->parent, parent);
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

static const VkGenFormat* vkgen_format_find(VkGenContext* ctx, const StringHash nameHash) {
  const VkGenFormat tgt = {.nameHash = nameHash};
  return dynarray_search_binary(&ctx->formats, vkgen_compare_format, &tgt);
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
  if (catHash == g_hash_basetype || catHash == g_hash_bitmask) {
    return VkGenTypeKind_Simple;
  }
  if (catHash == g_hash_funcpointer) {
    return VkGenTypeKind_FuncPointer;
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
    vkgen_type_push(ctx, VkGenTypeKind_Enum, name, 0 /* parent */, enumNode);
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
    const VkGenTypeKind kind   = vkgen_categorize_type(ctx, child);
    const StringHash    parent = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_parent);
    const String        name   = xml_attr_get(ctx->schemaDoc, child, g_hash_name);
    if (!string_is_empty(name)) {
      vkgen_type_push(ctx, kind, name, parent, child);
      continue;
    }
    const XmlNode nameNode = xml_child_get(ctx->schemaDoc, child, g_hash_name);
    if (!sentinel_check(nameNode)) {
      vkgen_type_push(ctx, kind, xml_value(ctx->schemaDoc, nameNode), parent, child);
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

static void vkgen_collect_custom_extensions(VkGenContext* ctx) {
  /**
   * Common (but non Khronos) PCI vendor ids.
   */
  static const struct {
    u32    vendorId;
    String name;
  } g_pciSigVendors[] = {
      {0x1002, string_static("VK_VENDOR_ID_AMD")},
      {0x1010, string_static("VK_VENDOR_ID_IMGTEC")},
      {0x10DE, string_static("VK_VENDOR_ID_NVIDIA")},
      {0x13B5, string_static("VK_VENDOR_ID_ARM")},
      {0x5143, string_static("VK_VENDOR_ID_QUALCOMM")},
      {0x8086, string_static("VK_VENDOR_ID_INTEL")},
  };
  VkGenEnumEntry enumEntry = {.key = string_hash_lit("VkVendorId")};
  for (u32 i = 0; i != array_elems(g_pciSigVendors); ++i) {
    enumEntry.name  = g_pciSigVendors[i].name;
    enumEntry.value = g_pciSigVendors[i].vendorId;
    vkgen_enum_entry_push(ctx, enumEntry);
  }
}

static void vkgen_collect_required_interfaces(
    VkGenContext*    ctx,
    DynBitSet*       markedCommands,
    const StringHash extensionType,
    const XmlNode    node) {
  const StringHash devTypeHash         = string_hash_lit("VkDevice");
  const StringHash devGetProcAddrHash  = string_hash_lit("vkGetDeviceProcAddr");
  const StringHash instTypeHash        = string_hash_lit("VkInstance");
  const StringHash instGetProcAddrHash = string_hash_lit("vkGetInstanceProcAddr");

  xml_for_children(ctx->schemaDoc, node, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_require) {
      continue; // Not a require element.
    }
    xml_for_children(ctx->schemaDoc, child, entry) {
      if (xml_name_hash(ctx->schemaDoc, entry) != g_hash_command) {
        continue; // Not a command element.
      }
      const StringHash cmdKey   = xml_attr_get_hash(ctx->schemaDoc, entry, g_hash_name);
      const u32        cmdIndex = vkgen_command_find(ctx, cmdKey);
      if (sentinel_check(cmdIndex)) {
        continue; // Unknown command.
      }
      if (dynbitset_test(markedCommands, cmdIndex)) {
        continue; // Already collected.
      }
      dynbitset_set(markedCommands, cmdIndex);

      const VkGenCommand* cmd = vkgen_command_get(ctx, cmdIndex);
      if (cmd->key == instGetProcAddrHash) {
        continue; // 'vkGetInstanceProcAddr' needs to be loaded from the dynamic library manually.
      }
      const XmlNode    firstParam    = xml_child_get(ctx->schemaDoc, cmd->schemaNode, g_hash_param);
      const XmlNode    firstTypeNode = xml_child_get(ctx->schemaDoc, firstParam, g_hash_type);
      const StringHash firstType     = string_hash(xml_value(ctx->schemaDoc, firstTypeNode));

      VkGenInterfaceCat cat;
      if (cmd->key == devGetProcAddrHash) {
        // 'vkGetDeviceProcAddr' is an exception that has to be handled by the instance.
        cat = VkGenInterfaceCat_Instance;
      } else if (vkgen_is_child(ctx, firstType, devTypeHash) && extensionType != g_hash_instance) {
        cat = VkGenInterfaceCat_Device;
      } else if (vkgen_is_child(ctx, firstType, instTypeHash)) {
        cat = VkGenInterfaceCat_Instance;
      } else {
        cat = VkGenInterfaceCat_loader;
      }

      *dynarray_push_t(&ctx->interfaces, VkGenInterface) = (VkGenInterface){
          .cat      = cat,
          .cmdIndex = cmdIndex,
      };
    }
  }
}

static void vkgen_collect_interfaces(VkGenContext* ctx) {
  DynBitSet markedCommands = dynbitset_create(g_allocHeap, ctx->commands.size);

  for (u32 i = 0; i != array_elems(g_vkgenFeatures); ++i) {
    const XmlNode featNode = ctx->featureNodes[i];
    vkgen_collect_required_interfaces(ctx, &markedCommands, 0 /* extensionType */, featNode);
  }
  for (u32 i = 0; i != array_elems(g_vkgenExtensions); ++i) {
    const XmlNode    extNode = ctx->extensionNodes[i];
    const StringHash extType = xml_attr_get_hash(ctx->schemaDoc, extNode, g_hash_type);
    vkgen_collect_required_interfaces(ctx, &markedCommands, extType, extNode);
  }

  dynbitset_destroy(&markedCommands);
  log_i("Collected interfaces", log_param("count", fmt_int(ctx->interfaces.size)));
}

static void vkgen_collect_formats(VkGenContext* ctx) {
  const XmlNode formatsNode = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_formats);
  xml_for_children(ctx->schemaDoc, formatsNode, node) {
    if (xml_name_hash(ctx->schemaDoc, node) != g_hash_format) {
      continue; // Not a format.
    }
    const String nameStr      = xml_attr_get(ctx->schemaDoc, node, g_hash_name);
    const String blockSizeStr = xml_attr_get(ctx->schemaDoc, node, g_hash_blockSize);

    u32 comps = 0;
    xml_for_children(ctx->schemaDoc, node, child) {
      if (xml_name_hash(ctx->schemaDoc, child) == g_hash_component) {
        ++comps;
      }
    }
    if (!comps) {
      log_w("Format {} has no components", log_param("name", fmt_text(nameStr)));
      continue;
    }
    u32 size = 0;
    if (!string_is_empty(blockSizeStr)) {
      size = (u32)vkgen_to_int(blockSizeStr);
    }
    if (!size) {
      log_w("Format {} has an invalid size", log_param("name", fmt_text(nameStr)));
      continue;
    }

    const bool   isCompressed      = xml_attr_has(ctx->schemaDoc, node, g_hash_compressed);
    const String texelsPerBlockStr = xml_attr_get(ctx->schemaDoc, node, g_hash_texelsPerBlock);

    *dynarray_push_t(&ctx->formats, VkGenFormat) = (VkGenFormat){
        .nameHash      = string_hash(nameStr),
        .comps         = comps,
        .size          = size,
        .compressed4x4 = isCompressed && vkgen_to_int(texelsPerBlockStr) == 16,
    };
  }
  dynarray_sort(&ctx->formats, vkgen_compare_format);
  log_i("Collected formats", log_param("count", fmt_int(ctx->formats.size)));
}

static String vkgen_ref_scratch(const VkGenRef* ref) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString str        = dynstring_create_over(scratchMem);
  if (ref->flags & VkGenRef_Const) {
    fmt_write(&str, "const ");
  }
  fmt_write(&str, "{}", fmt_text(ref->name));
  if (ref->flags & VkGenRef_Pointer) {
    fmt_write(&str, "*");
  }
  return dynstring_view(&str);
}

static void vkgen_ref_resolve_alias(VkGenRef* ref) {
  for (u32 i = 0; i != array_elems(g_vkgenRefAliases); ++i) {
    const String org = g_vkgenRefAliases[i].original;
    if (string_eq(org, ref->name)) {
      ref->name = g_vkgenRefAliases[i].replacement;
      if (g_vkgenRefAliases[i].stripPointer) {
        ref->flags &= ~(VkGenRef_Const | VkGenRef_Pointer);
      }
      break;
    }
  }
}

static bool vkgen_ref_read(VkGenContext* ctx, XmlNode* node, VkGenRef* out) {
  const bool    isConst  = vkgen_node_value_match(ctx->schemaDoc, *node, string_lit("const"));
  const XmlNode typeNode = isConst ? xml_next(ctx->schemaDoc, *node) : *node;

  if (xml_name_hash(ctx->schemaDoc, typeNode) != g_hash_type) {
    return false; // Not a type.
  }
  *out = (VkGenRef){
      .flags = isConst ? VkGenRef_Const : 0,
      .name  = string_trim_whitespace(xml_value(ctx->schemaDoc, typeNode)),
  };
  if (vkgen_node_value_match(ctx->schemaDoc, xml_next(ctx->schemaDoc, typeNode), string_lit("*"))) {
    out->flags |= VkGenRef_Pointer;
    *node = xml_next(ctx->schemaDoc, typeNode);
  } else {
    *node = typeNode;
  }
  vkgen_ref_resolve_alias(out);
  return true;
}

static void vkgen_write_node_itr(VkGenContext* ctx, XmlNode* nodeItr) {
  if (xml_name_hash(ctx->schemaDoc, *nodeItr) == g_hash_comment) {
    return; // Skip comments.
  }
  VkGenRef ref;
  String   text;
  bool     needSeparator = false;
  if (vkgen_ref_read(ctx, nodeItr, &ref)) {
    text          = vkgen_ref_scratch(&ref);
    needSeparator = true;
  } else if (xml_name_hash(ctx->schemaDoc, *nodeItr) == g_hash_name) {
    text          = xml_value(ctx->schemaDoc, *nodeItr);
    needSeparator = true;
  } else {
    text = vkgen_collapse_whitespace_scratch(xml_value(ctx->schemaDoc, *nodeItr));
  }
  if (needSeparator && !vkgen_out_last_is_separator(ctx)) {
    fmt_write(&ctx->out, " ");
  }
  fmt_write(&ctx->out, "{}", fmt_text(text, .flags = FormatTextFlags_SingleLine));
}

static void vkgen_write_node_children(VkGenContext* ctx, const XmlNode node) {
  xml_for_children(ctx->schemaDoc, node, child) { vkgen_write_node_itr(ctx, &child); }
}

static bool vkgen_write_type_func_pointer(VkGenContext* ctx, const VkGenType* type) {
  XmlNode child = xml_first_child(ctx->schemaDoc, type->schemaNode);
  String  text  = xml_value(ctx->schemaDoc, child);
  if (!string_starts_with(text, string_lit("typedef "))) {
    return false; // Malformed func pointer typedef.
  }
  text = string_consume(text, string_lit("typedef ").size);

  const usize typeEnd = string_find_first(text, string_lit("("));
  if (sentinel_check(typeEnd)) {
    return false; // Malformed func pointer typedef.
  }
  const String retType = string_trim_whitespace(string_slice(text, 0, typeEnd));

  child = xml_next(ctx->schemaDoc, child);
  if (!vkgen_node_value_match(ctx->schemaDoc, child, type->name)) {
    return false; // Unexpected type-def name.
  }

  fmt_write(&ctx->out, "typedef {} (SYS_DECL* {})(", fmt_text(retType), fmt_text(type->name));

  child = xml_next(ctx->schemaDoc, child);
  if (vkgen_node_value_match(ctx->schemaDoc, child, string_lit(")(void);"))) {
    fmt_write(&ctx->out, "void);\n\n");
    return true;
  }
  if (!vkgen_node_value_match(ctx->schemaDoc, child, string_lit(")("))) {
    return false; // Malformed func pointer typedef.
  }
  child = xml_next(ctx->schemaDoc, child);
  for (; !sentinel_check(child); child = xml_next(ctx->schemaDoc, child)) {
    vkgen_write_node_itr(ctx, &child);
  }
  fmt_write(&ctx->out, "\n\n");
  return true;
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
    vkgen_write_node_children(ctx, entry);
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
    vkgen_write_node_children(ctx, entry);
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
    return true; // No output needed.
  case VkGenTypeKind_Simple:
    vkgen_write_node_children(ctx, type->schemaNode);
    fmt_write(&ctx->out, "\n\n");
    return true;
  case VkGenTypeKind_FuncPointer:
    return vkgen_write_type_func_pointer(ctx, type);
  case VkGenTypeKind_Handle:
    fmt_write(&ctx->out, "typedef struct {}_T* {};\n\n", fmt_text(name), fmt_text(name));
    return true;
  case VkGenTypeKind_Enum:
    return vkgen_write_type_enum(ctx, type), true;
  case VkGenTypeKind_Struct:
    return vkgen_write_type_struct(ctx, type), true;
  case VkGenTypeKind_Union:
    return vkgen_write_type_union(ctx, type), true;
  }
  UNREACHABLE
}

static bool vkgen_write_required_types(VkGenContext* ctx, const XmlNode node) {
  bool success = true;
  xml_for_children(ctx->schemaDoc, node, set) {
    if (xml_name_hash(ctx->schemaDoc, set) != g_hash_require) {
      continue; // Not a require element.
    }
    xml_for_children(ctx->schemaDoc, set, entry) {
      const StringHash entryNameHash = xml_name_hash(ctx->schemaDoc, entry);
      if (entryNameHash == g_hash_type) {
        const String typeName = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
        if (!vkgen_write_type(ctx, string_hash(typeName))) {
          log_e("Failed to write type", log_param("name", fmt_text(typeName)));
          success = false;
        }
        continue;
      }
      if (entryNameHash == g_hash_command) {
        const String cmdName  = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
        const u32    cmdIndex = vkgen_command_find(ctx, string_hash(cmdName));
        if (sentinel_check(cmdIndex)) {
          log_e("Unkown command", log_param("name", fmt_text(cmdName)));
          success = false;
          continue;
        }
        const VkGenCommand* cmd = vkgen_command_get(ctx, cmdIndex);
        if (!vkgen_write_dependencies(ctx, cmd->schemaNode)) {
          log_e("Command dependencies missing", log_param("name", fmt_text(cmdName)));
          success = false;
        }
        continue;
      }
    }
  }
  return success;
}

static bool vkgen_write_used_types(VkGenContext* ctx) {
  // Write types required for features.
  for (u32 i = 0; i != array_elems(g_vkgenFeatures); ++i) {
    if (!vkgen_write_required_types(ctx, ctx->featureNodes[i])) {
      return false; // Feature requirement missing.
    }
  }
  // Write types required for extensions.
  for (u32 i = 0; i != array_elems(g_vkgenExtensions); ++i) {
    if (!vkgen_write_required_types(ctx, ctx->extensionNodes[i])) {
      return false; // Extension requirement missing.
    }
  }
  return true;
}

static void vkgen_write_stringify_decl(VkGenContext* ctx, const VkGenStringify* entry) {
  const String funcName = fmt_write_scratch(
      "{}{}Str",
      fmt_char(ascii_to_lower(*string_begin(entry->typeName))),
      fmt_text(string_consume(entry->typeName, 1)));

  fmt_write(&ctx->out, "String {}({});\n", fmt_text(funcName), fmt_text(entry->typeName));
}

static void vkgen_write_stringify_def(VkGenContext* ctx, const VkGenStringify* entry) {
  const String funcName = fmt_write_scratch(
      "{}{}Str",
      fmt_char(ascii_to_lower(*string_begin(entry->typeName))),
      fmt_text(string_consume(entry->typeName, 1)));

  VkGenEnumEntries enumEntries = vkgen_enum_entries_find(ctx, string_hash(entry->typeName));

  fmt_write(&ctx->out, "String {}(const {} v) {\n", fmt_text(funcName), fmt_text(entry->typeName));
  fmt_write(&ctx->out, "  switch(v) {\n");
  for (const VkGenEnumEntry* itr = enumEntries.begin; itr != enumEntries.end; ++itr) {
    String val = itr->name;
    if (string_starts_with(val, entry->entryPrefix)) {
      val = string_consume(val, entry->entryPrefix.size);
    }
    fmt_write(
        &ctx->out,
        "    case {}: return string_lit(\"{}\");\n",
        fmt_text(itr->name),
        fmt_text(val, .flags = FormatTextFlags_ToLower));
  }
  fmt_write(&ctx->out, "    default: return string_lit(\"unknown\");\n");
  fmt_write(&ctx->out, "  }\n}\n\n");
}

static bool vkgen_write_format_info_def(VkGenContext* ctx) {
  VkGenEnumEntries enumEntries = vkgen_enum_entries_find(ctx, string_hash_lit("VkFormat"));
  if (enumEntries.begin == enumEntries.end) {
    log_e("Format enum missing");
    return false; // Format enum missing.
  }

  // Write vkFormatByteSize definition.
  fmt_write(&ctx->out, "u32 vkFormatByteSize(const VkFormat f) {\n");
  fmt_write(&ctx->out, "  switch(f) {\n");
  for (const VkGenEnumEntry* itr = enumEntries.begin; itr != enumEntries.end; ++itr) {
    const VkGenFormat* info = vkgen_format_find(ctx, string_hash(itr->name));
    if (info) {
      fmt_write(&ctx->out, "    case {}: return {};\n", fmt_text(itr->name), fmt_int(info->size));
    }
  }
  fmt_write(&ctx->out, "    default: return sentinel_u32;\n");
  fmt_write(&ctx->out, "  }\n}\n\n");

  // Write vkFormatComponents definition.
  fmt_write(&ctx->out, "u32 vkFormatComponents(const VkFormat f) {\n");
  fmt_write(&ctx->out, "  switch(f) {\n");
  for (const VkGenEnumEntry* itr = enumEntries.begin; itr != enumEntries.end; ++itr) {
    const VkGenFormat* info = vkgen_format_find(ctx, string_hash(itr->name));
    if (info) {
      fmt_write(&ctx->out, "    case {}: return {};\n", fmt_text(itr->name), fmt_int(info->comps));
    }
  }
  fmt_write(&ctx->out, "    default: return sentinel_u32;\n");
  fmt_write(&ctx->out, "  }\n}\n\n");

  // Write vkFormatCompressed4x4 definition.
  fmt_write(&ctx->out, "bool vkFormatCompressed4x4(const VkFormat f) {\n");
  fmt_write(&ctx->out, "  switch(f) {\n");
  for (const VkGenEnumEntry* itr = enumEntries.begin; itr != enumEntries.end; ++itr) {
    const VkGenFormat* info = vkgen_format_find(ctx, string_hash(itr->name));
    if (info && info->compressed4x4) {
      fmt_write(&ctx->out, "    case {}:\n", fmt_text(itr->name), fmt_int(info->comps));
    }
  }
  fmt_write(&ctx->out, "      return true;\n");
  fmt_write(&ctx->out, "    default:\n      return false;\n");
  fmt_write(&ctx->out, "  }\n}\n\n");

  return true;
}

static String vkgen_interface_cat_name(const VkGenInterfaceCat cat) {
  switch (cat) {
  case VkGenInterfaceCat_loader:
    return string_lit("Loader");
  case VkGenInterfaceCat_Instance:
    return string_lit("Instance");
  case VkGenInterfaceCat_Device:
    return string_lit("Device");
  case VkGenInterfaceCat_Count:
    break;
  }
  UNREACHABLE
}

static bool vkgen_write_interface(VkGenContext* ctx, const VkGenInterfaceCat category) {
  const String catName = vkgen_interface_cat_name(category);
  fmt_write(&ctx->out, "typedef struct VkInterface{} {\n", fmt_text(catName));

  dynarray_for_t(&ctx->interfaces, VkGenInterface, interface) {
    if (interface->cat != category) {
      continue;
    }
    const VkGenCommand* cmd = vkgen_command_get(ctx, interface->cmdIndex);

    String varName = cmd->name;
    if (string_starts_with(varName, string_lit("vk")) && varName.size >= 3) {
      varName = fmt_write_scratch(
          "{}{}",
          fmt_char(ascii_to_lower(*string_at(varName, 2))),
          fmt_text(string_consume(varName, 3)));
    }

    fmt_write(&ctx->out, "  {} (SYS_DECL* {})(", fmt_text(cmd->type), fmt_text(varName));
    bool anyParam = false;
    xml_for_children(ctx->schemaDoc, cmd->schemaNode, child) {
      if (xml_name_hash(ctx->schemaDoc, child) != g_hash_param) {
        continue; // Not a parameter.
      }
      if (!vkgen_is_supported_api(ctx, child)) {
        continue; // Not supported.
      }
      if (anyParam) {
        fmt_write(&ctx->out, ", ");
      }
      vkgen_write_node_children(ctx, child);
      anyParam = true;
    }
    if (!anyParam) {
      fmt_write(&ctx->out, "void");
    }
    fmt_write(&ctx->out, ");\n");
  }

  fmt_write(&ctx->out, "} VkInterface{};\n\n", fmt_text(catName));
  return true;
}

static void vkgen_write_prolog(VkGenContext* ctx) {
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
  fmt_write(&ctx->out, "\n");
}

static bool vkgen_write_header(VkGenContext* ctx) {
  fmt_write(&ctx->out, "#pragma once\n");
  fmt_write(&ctx->out, "// clang-format off\n");
  vkgen_write_prolog(ctx);

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

  // Write types required for features.
  if (!vkgen_write_used_types(ctx)) {
    return false;
  }

  // Write stringify declarations.
  array_for_t(g_vkgenStringify, VkGenStringify, entry) { vkgen_write_stringify_decl(ctx, entry); }

  // Write format-info declarations.
  fmt_write(&ctx->out, "u32 vkFormatByteSize(VkFormat);\n");
  fmt_write(&ctx->out, "u32 vkFormatComponents(VkFormat);\n");
  fmt_write(&ctx->out, "bool vkFormatCompressed4x4(VkFormat);\n");
  fmt_write(&ctx->out, "\n");

  // Write interface declarations.
  for (VkGenInterfaceCat cat = 0; cat != VkGenInterfaceCat_Count; ++cat) {
    if (!vkgen_write_interface(ctx, cat)) {
      return false;
    }
  }

  fmt_write(&ctx->out, "// clang-format on\n");
  return true;
}

static bool vkgen_write_impl(VkGenContext* ctx) {
  fmt_write(&ctx->out, "// clang-format off\n");
  vkgen_write_prolog(ctx);

  fmt_write(&ctx->out, "#include \"{}.h\"\n", fmt_text(ctx->outName));
  fmt_write(&ctx->out, "#include \"core_sentinel.h\"\n\n");
  fmt_write(&ctx->out, "#include \"core_string.h\"\n\n");

  // Write stringify definitions.
  array_for_t(g_vkgenStringify, VkGenStringify, entry) { vkgen_write_stringify_def(ctx, entry); }

  // Write format-info definitions.
  if (!vkgen_write_format_info_def(ctx)) {
    return false;
  }

  fmt_write(&ctx->out, "// clang-format on\n");
  return true;
}

// clang-format off
static const String g_appDesc = string_static("VulkanGen - Utility to generate a Vulkan api header and utility c file.");
static const String g_schemaDefaultHost = string_static("raw.githubusercontent.com");
static const String g_schemaDefaultUri  = string_static("/KhronosGroup/Vulkan-Docs/refs/tags/v1.4.308/xml/vk.xml");
// clang-format on

static CliId g_optVerbose, g_optOutputPath, g_optSchemaHost, g_optSchemaUri, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, g_appDesc);

  g_optVerbose = cli_register_flag(app, 'v', string_lit("verbose"), CliOptionFlags_None);

  g_optOutputPath = cli_register_arg(app, string_lit("output-path"), CliOptionFlags_Required);
  cli_register_desc(
      app,
      g_optOutputPath,
      string_lit("Path for the header and c file (.h and .c is automatically appended)."));

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
  const String outputPath = cli_read_string(invoc, g_optOutputPath, string_empty);
  if (string_is_empty(outputPath)) {
    file_write_sync(g_fileStdErr, string_lit("Output path missing.\n"));
    return 1;
  }

  vkgen_init_hashes();
  net_init();

  bool success = false;

  const LogMask logMask = cli_parse_provided(invoc, g_optVerbose) ? LogMask_All : ~LogMask_Debug;
  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, logMask));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  VkGenContext ctx = {
      .schemaDoc    = xml_create(g_allocHeap, 128 * 1024),
      .schemaHost   = cli_read_string(invoc, g_optSchemaHost, g_schemaDefaultHost),
      .schemaUri    = cli_read_string(invoc, g_optSchemaUri, g_schemaDefaultUri),
      .types        = dynarray_create_t(g_allocHeap, VkGenType, 4096),
      .typesWritten = dynbitset_create(g_allocHeap, 4096),
      .constants    = dynarray_create_t(g_allocHeap, VkGenConstant, 64),
      .enumEntries  = dynarray_create_t(g_allocHeap, VkGenEnumEntry, 2048),
      .commands     = dynarray_create_t(g_allocHeap, VkGenCommand, 1024),
      .interfaces   = dynarray_create_t(g_allocHeap, VkGenInterface, 512),
      .formats      = dynarray_create_t(g_allocHeap, VkGenFormat, 512),
      .outName      = path_stem(outputPath),
      .out          = dynstring_create(g_allocHeap, usize_kibibyte * 16),
  };

  ctx.schemaRoot = vkgen_schema_get(ctx.schemaDoc, ctx.schemaHost, ctx.schemaUri);
  if (sentinel_check(ctx.schemaRoot)) {
    goto Exit;
  }

  vkgen_collect_constants(&ctx);
  vkgen_collect_enums(&ctx);
  vkgen_collect_types(&ctx);
  vkgen_collect_commands(&ctx);
  vkgen_collect_features(&ctx);
  vkgen_collect_extensions(&ctx);
  vkgen_collect_custom_extensions(&ctx);
  vkgen_collect_formats(&ctx);
  vkgen_collect_interfaces(&ctx);

  // Verify we found all needed features.
  for (u32 i = 0; i != array_elems(g_vkgenFeatures); ++i) {
    if (sentinel_check(ctx.featureNodes[i])) {
      log_e("Feature not found", log_param("name", fmt_text(g_vkgenFeatures[i])));
      goto Exit;
    }
  }

  // Verify we found all needed extensions.
  for (u32 i = 0; i != array_elems(g_vkgenExtensions); ++i) {
    if (sentinel_check(ctx.extensionNodes[i])) {
      log_e("Extension not found", log_param("name", fmt_text(g_vkgenExtensions[i])));
      goto Exit;
    }
  }

  if (vkgen_write_header(&ctx)) {
    const String headerPath = fmt_write_scratch("{}.h", fmt_text(outputPath));
    if (file_write_to_path_sync(headerPath, dynstring_view(&ctx.out)) == FileResult_Success) {
      log_i("Generated header", log_param("path", fmt_path(headerPath)));
      success = true;
    }
  } else {
    log_e("Failed to write header");
    goto Exit;
  }
  dynstring_clear(&ctx.out);
  if (vkgen_write_impl(&ctx)) {
    const String implPath = fmt_write_scratch("{}.c", fmt_text(outputPath));
    if (file_write_to_path_sync(implPath, dynstring_view(&ctx.out)) == FileResult_Success) {
      log_i("Generated implementation", log_param("path", fmt_path(implPath)));
      success = true;
    }
  } else {
    log_e("Failed to write implementation");
    goto Exit;
  }

Exit:
  xml_destroy(ctx.schemaDoc);
  dynarray_destroy(&ctx.types);
  dynbitset_destroy(&ctx.typesWritten);
  dynarray_destroy(&ctx.constants);
  dynarray_destroy(&ctx.enumEntries);
  dynarray_destroy(&ctx.commands);
  dynarray_destroy(&ctx.interfaces);
  dynarray_destroy(&ctx.formats);
  dynstring_destroy(&ctx.out);

  net_teardown();
  return success ? 0 : 1;
}
