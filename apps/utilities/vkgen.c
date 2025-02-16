#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
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
  VKGEN_HASH(offset)                                                                               \
  VKGEN_HASH(param)                                                                                \
  VKGEN_HASH(proto)                                                                                \
  VKGEN_HASH(require)                                                                              \
  VKGEN_HASH(struct)                                                                               \
  VKGEN_HASH(supported)                                                                            \
  VKGEN_HASH(type)                                                                                 \
  VKGEN_HASH(types)                                                                                \
  VKGEN_HASH(union)                                                                                \
  VKGEN_HASH(value)                                                                                \
  VKGEN_HASH(vk_platform)

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
  StringHash enumKey;
  String     name; // Allocated in the schema document.
  i64        value;
} VkGenAddition;

typedef struct {
  XmlDoc*   schemaDoc;
  XmlNode   schemaRoot;
  String    schemaHost, schemaUri;
  DynArray  types; // VkGenType[]
  DynBitSet typesWritten;
  DynArray  enums;     // VkGenType[]
  DynArray  additions; // VkGenAddition[]
  DynArray  commands;  // VkGenType[]
  DynBitSet commandsWritten;
  DynArray  extensions; // VkGenType[]
  DynBitSet extensionsWritten;
  DynArray  features; // VkGenType[]
  DynBitSet featuresWritten;
  DynString out;
} VkGenContext;

static i8 vkgen_compare_entry(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, VkGenEntry, key), field_ptr(b, VkGenEntry, key));
}

static i8 vkgen_compare_addition(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, VkGenAddition, enumKey), field_ptr(b, VkGenAddition, enumKey));
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

static u32 vkgen_entry_index(DynArray* arr, const StringHash key) {
  const VkGenEntry tgt = {.key = key};
  VkGenEntry*      res = dynarray_search_binary(arr, vkgen_compare_entry, &tgt);
  return res ? (u32)(res - dynarray_begin_t(arr, VkGenEntry)) : sentinel_u32;
}

static void vkgen_addition_push(
    VkGenContext* ctx, const StringHash enumKey, const String name, const i64 value) {
  *dynarray_push_t(&ctx->additions, VkGenAddition) = (VkGenAddition){
      .enumKey = enumKey,
      .name    = name,
      .value   = value,
  };
}

static void vkgen_addition_collect(VkGenContext* ctx, const XmlNode node) {
  xml_for_children(ctx->schemaDoc, node, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_require) {
      continue; // Not a require element.
    }
    xml_for_children(ctx->schemaDoc, child, entry) {
      const StringHash entryNameHash = xml_name_hash(ctx->schemaDoc, entry);
      if (entryNameHash == g_hash_enum) {
        const StringHash enumKey = xml_attr_get_hash(ctx->schemaDoc, entry, g_hash_extends);
        const String     name    = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
        if (!enumKey || string_is_empty(name)) {
          continue; // Enum or name missing.
        }
        const String valueStr = xml_attr_get(ctx->schemaDoc, entry, g_hash_value);
        if (!string_is_empty(valueStr)) {
          i64 value;
          format_read_i64(valueStr, &value, 10 /* base */);
          vkgen_addition_push(ctx, enumKey, name, value);
          continue;
        }
        const String offsetStr    = xml_attr_get(ctx->schemaDoc, entry, g_hash_offset);
        const String extnumberStr = xml_attr_get(ctx->schemaDoc, entry, g_hash_extnumber);
        if (!string_is_empty(offsetStr) && !string_is_empty(extnumberStr)) {
          i64 offset;
          format_read_i64(offsetStr, &offset, 10 /* base */);
          i64 extnumber;
          format_read_i64(extnumberStr, &extnumber, 10 /* base */);
          const i64 value = 1000000000 + (extnumber - 1) * 1000 + offset;
          vkgen_addition_push(ctx, enumKey, name, value);
        }
      }
    }
  }
}

typedef struct {
  const VkGenAddition* begin;
  const VkGenAddition* end;
} VkGenAdditionSet;

static VkGenAdditionSet vkgen_addition_find(VkGenContext* ctx, const StringHash enumKey) {
  const VkGenAddition* addBegin = dynarray_begin_t(&ctx->additions, VkGenAddition);
  const VkGenAddition* addEnd   = dynarray_end_t(&ctx->additions, VkGenAddition);
  if (addBegin == addEnd) {
    return (VkGenAdditionSet){0};
  }
  VkGenAddition        tgt = {.enumKey = enumKey};
  const VkGenAddition* greater =
      search_binary_greater_t(addBegin, addEnd, VkGenAddition, vkgen_compare_addition, &tgt);

  VkGenAdditionSet res = {
      .begin = greater ? greater : addEnd,
      .end   = greater ? greater : addEnd,
  };
  if (res.begin == addBegin) {
    return (VkGenAdditionSet){0};
  }
  for (; res.begin != addBegin && (res.begin - 1)->enumKey == enumKey; --res.begin)
    ;
  return res;
}

static bool vkgen_contains(String str, const String other) {
  while (!string_is_empty(str)) {
    usize len = string_find_first_char(str, ',');
    if (sentinel_check(len)) {
      len = str.size;
    }
    const String api = string_slice(str, 0, len);
    if (string_eq(api, other)) {
      return true;
    }
    str = string_consume(str, len);
  }
  return false;
}

static bool vkgen_is_supported(VkGenContext* ctx, const XmlNode node) {
  const String apis = xml_attr_get(ctx->schemaDoc, node, g_hash_supported);
  return string_is_empty(apis) || vkgen_contains(apis, string_lit("vulkan"));
}

static bool vkgen_is_supported_api(VkGenContext* ctx, const XmlNode node) {
  const String apis = xml_attr_get(ctx->schemaDoc, node, g_hash_api);
  return string_is_empty(apis) || vkgen_contains(apis, string_lit("vulkan"));
}

static bool vkgen_is_deprecated(VkGenContext* ctx, const XmlNode node) {
  return !string_is_empty(xml_attr_get(ctx->schemaDoc, node, g_hash_deprecated));
}

static void vkgen_collect_types(VkGenContext* ctx) {
  const XmlNode typesNode = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_types);
  xml_for_children(ctx->schemaDoc, typesNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_type) {
      if (!vkgen_is_supported_api(ctx, child)) {
        continue;
      }
      const StringHash nameHash = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
      if (nameHash) {
        vkgen_entry_push(&ctx->types, nameHash, child);
        continue;
      }
      const XmlNode nameNode = xml_child_get(ctx->schemaDoc, child, g_hash_name);
      if (!sentinel_check(nameNode)) {
        const String nameText = xml_child_text(ctx->schemaDoc, nameNode);
        vkgen_entry_push(&ctx->types, string_hash(nameText), child);
        continue;
      }
    }
  }
  dynarray_sort(&ctx->types, vkgen_compare_entry);
  log_i("Collected types", log_param("count", fmt_int(ctx->types.size)));
}

static void vkgen_collect_enums(VkGenContext* ctx) {
  xml_for_children(ctx->schemaDoc, ctx->schemaRoot, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_enums) {
      if (!vkgen_is_supported_api(ctx, child)) {
        continue;
      }
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
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_command) {
      continue; // Not a command element.
    }
    if (!vkgen_is_supported_api(ctx, child)) {
      continue;
    }
    const XmlNode protoNode = xml_child_get(ctx->schemaDoc, child, g_hash_proto);
    if (sentinel_check(protoNode)) {
      continue; // Command without a proto (could be an alias).
    }
    const XmlNode protoNameNode = xml_child_get(ctx->schemaDoc, protoNode, g_hash_name);
    if (sentinel_check(protoNameNode)) {
      continue; // Name missing.
    }
    const String name = xml_child_text(ctx->schemaDoc, protoNameNode);
    if (!string_is_empty(name)) {
      vkgen_entry_push(&ctx->commands, string_hash(name), child);
    }
  }
  dynarray_sort(&ctx->commands, vkgen_compare_entry);
  log_i("Collected commands", log_param("count", fmt_int(ctx->commands.size)));
}

static void vkgen_collect_extensions(VkGenContext* ctx) {
  const XmlNode extensionsNode = xml_child_get(ctx->schemaDoc, ctx->schemaRoot, g_hash_extensions);
  xml_for_children(ctx->schemaDoc, extensionsNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_extension) {
      continue; // Not an extension element.
    }
    if (!vkgen_is_supported(ctx, child)) {
      continue;
    }
    const StringHash nameHash = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
    if (nameHash) {
      vkgen_entry_push(&ctx->extensions, nameHash, child);
      vkgen_addition_collect(ctx, child); // TODO: Only collect additions for enabled extensions.
    }
  }
  dynarray_sort(&ctx->extensions, vkgen_compare_entry);
  log_i("Collected extensions", log_param("count", fmt_int(ctx->extensions.size)));
}

static void vkgen_collect_features(VkGenContext* ctx) {
  xml_for_children(ctx->schemaDoc, ctx->schemaRoot, child) {
    if (xml_name_hash(ctx->schemaDoc, child) == g_hash_feature) {
      if (!vkgen_is_supported_api(ctx, child)) {
        continue;
      }
      const StringHash nameHash = xml_attr_get_hash(ctx->schemaDoc, child, g_hash_name);
      if (nameHash) {
        vkgen_entry_push(&ctx->features, nameHash, child);
        vkgen_addition_collect(ctx, child); // TODO: Only collect additions for enabled features.
      }
    }
  }
  dynarray_sort(&ctx->features, vkgen_compare_entry);
  log_i("Collected features", log_param("count", fmt_int(ctx->features.size)));
}

static void vkgen_collect_additions(VkGenContext* ctx) {
  // NOTE: Additions are pushed during the other collection phases.
  dynarray_sort(&ctx->additions, vkgen_compare_addition);
  log_i("Collected additions", log_param("count", fmt_int(ctx->additions.size)));
}

static void vkgen_write_comment_elem(VkGenContext* ctx, const XmlNode comment) {
  const XmlNode text = xml_first_child(ctx->schemaDoc, comment);
  if (xml_is(ctx->schemaDoc, text, XmlType_Text)) {
    const String str = string_trim_whitespace(xml_value(ctx->schemaDoc, text));
    fmt_write(&ctx->out, "// {}.\n", fmt_text(str, .flags = FormatTextFlags_SingleLine));
  }
}

static bool vkgen_write_enum(VkGenContext* ctx, const StringHash key) {
  const XmlNode    node      = vkgen_entry_find(&ctx->enums, key);
  VkGenAdditionSet additions = vkgen_addition_find(ctx, key);
  if (sentinel_check(xml_first_child(ctx->schemaDoc, node)) && additions.begin == additions.end) {
    return true; // Empty enum.
  }
  DynArray writtenNames = dynarray_create_t(g_allocScratch, StringHash, 512);

  const StringHash typeHash = xml_attr_get_hash(ctx->schemaDoc, node, g_hash_type);
  if (typeHash == g_hash_enum || typeHash == g_hash_bitmask) {
    fmt_write(&ctx->out, "typedef enum {\n");
    xml_for_children(ctx->schemaDoc, node, entry) {
      if (xml_name_hash(ctx->schemaDoc, entry) != g_hash_enum || vkgen_is_deprecated(ctx, entry)) {
        continue; // Not an enum entry or a deprecated entry.
      }
      const String     name   = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
      const StringHash nameHs = string_hash(name);
      *dynarray_insert_sorted_t(&writtenNames, StringHash, compare_stringhash, &nameHs) = nameHs;

      fmt_write(&ctx->out, "  {}", fmt_text(name));

      const String val    = xml_attr_get(ctx->schemaDoc, entry, g_hash_value);
      const String bitPos = xml_attr_get(ctx->schemaDoc, entry, g_hash_bitpos);
      if (!string_is_empty(val)) {
        fmt_write(&ctx->out, " = {}", fmt_text(val));
      } else if (!string_is_empty(bitPos)) {
        fmt_write(&ctx->out, " = 1 << {}", fmt_text(bitPos));
      }
      fmt_write(&ctx->out, ",\n");
    }
    for (const VkGenAddition* itr = additions.begin; itr != additions.end; ++itr) {
      const StringHash nameHs = string_hash(itr->name);
      if (dynarray_search_binary(&writtenNames, compare_stringhash, &nameHs)) {
        continue; // Duplicate name.
      }
      *dynarray_insert_sorted_t(&writtenNames, StringHash, compare_stringhash, &nameHs) = nameHs;
      fmt_write(&ctx->out, "  {} = {},\n", fmt_text(itr->name), fmt_int(itr->value));
    }
    fmt_write(&ctx->out, "} {};\n\n", fmt_text(xml_attr_get(ctx->schemaDoc, node, g_hash_name)));
    return true;
  }
  if (typeHash == g_hash_constants) {
    xml_for_children(ctx->schemaDoc, node, entry) {
      if (xml_name_hash(ctx->schemaDoc, entry) == g_hash_enum) {
        const String name = xml_attr_get(ctx->schemaDoc, entry, g_hash_name);
        const String val  = xml_attr_get(ctx->schemaDoc, entry, g_hash_value);
        fmt_write(&ctx->out, "#define {} {}\n", fmt_text(name), fmt_text(val));
      }
    }
    fmt_write(&ctx->out, "\n");
    return true;
  }
  return false; // Unsupported enum type.
}

static void vkgen_write_node(VkGenContext* ctx, const XmlNode node) {
  bool lastIsElement = false;
  xml_for_children(ctx->schemaDoc, node, part) {
    switch (xml_type(ctx->schemaDoc, part)) {
    case XmlType_Element: {
      if (xml_name_hash(ctx->schemaDoc, part) == g_hash_comment) {
        continue;
      }
      if (lastIsElement) {
        fmt_write(&ctx->out, " ");
      }
      lastIsElement = true;
      fmt_write(&ctx->out, "{}", fmt_text(xml_child_text(ctx->schemaDoc, part)));
    } break;
    case XmlType_Text:
      fmt_write(&ctx->out, "{}", fmt_text(xml_value(ctx->schemaDoc, part)));
      lastIsElement = false;
      break;
    default:
      break;
    }
  }
}

static void vkgen_write_type_struct(VkGenContext* ctx, const XmlNode typeNode) {
  const String typeName = xml_attr_get(ctx->schemaDoc, typeNode, g_hash_name);
  fmt_write(&ctx->out, "typedef struct {} {\n", fmt_text(typeName));

  xml_for_children(ctx->schemaDoc, typeNode, entry) {
    const StringHash nameHash = xml_name_hash(ctx->schemaDoc, entry);
    if (nameHash != g_hash_member || !vkgen_is_supported_api(ctx, entry)) {
      continue; // Not a struct member or not a supported member.
    }
    fmt_write(&ctx->out, "  ");
    vkgen_write_node(ctx, entry);
    fmt_write(&ctx->out, ";\n");
  }
  fmt_write(&ctx->out, "} {};\n\n", fmt_text(typeName));
}

static void vkgen_write_type_union(VkGenContext* ctx, const XmlNode typeNode) {
  const String typeName = xml_attr_get(ctx->schemaDoc, typeNode, g_hash_name);
  fmt_write(&ctx->out, "typedef union {} {\n", fmt_text(typeName));

  xml_for_children(ctx->schemaDoc, typeNode, entry) {
    const StringHash nameHash = xml_name_hash(ctx->schemaDoc, entry);
    if (nameHash != g_hash_member || !vkgen_is_supported_api(ctx, entry)) {
      continue; // Not a union member or not a supported member.
    }
    fmt_write(&ctx->out, "  ");
    vkgen_write_node(ctx, entry);
    fmt_write(&ctx->out, ";\n");
  }
  fmt_write(&ctx->out, "} {};\n\n", fmt_text(typeName));
}

static bool vkgen_write_type(VkGenContext*, StringHash key);

static bool vkgen_write_type_dependencies(VkGenContext* ctx, const XmlNode typeNode) {
  bool success = true;
  xml_for_children(ctx->schemaDoc, typeNode, entry) {
    if (xml_type(ctx->schemaDoc, entry) != XmlType_Element) {
      continue; // Not an element.
    }
    if (xml_name_hash(ctx->schemaDoc, entry) == g_hash_type) {
      const String innerText = xml_child_text(ctx->schemaDoc, entry);
      success &= vkgen_write_type(ctx, string_hash(innerText));
      continue;
    }
    success &= vkgen_write_type_dependencies(ctx, entry);
  }
  return success;
}

static bool vkgen_write_include(VkGenContext* ctx, const StringHash key) {
  if (key == g_hash_vk_platform) {
    fmt_write(&ctx->out, "#define VKAPI_ATTR\n");
    fmt_write(&ctx->out, "#define VKAPI_CALL SYS_DECL\n");
    fmt_write(&ctx->out, "#define VKAPI_PTR SYS_DECL\n");
    fmt_write(&ctx->out, "\n");
    return true;
  }
  return false;
}

static bool vkgen_write_type(VkGenContext* ctx, const StringHash key) {
  const u32 typeIndex = vkgen_entry_index(&ctx->types, key);
  if (sentinel_check(typeIndex)) {
    return false; // Unknown type.
  }
  if (dynbitset_test(&ctx->typesWritten, typeIndex)) {
    return true; // Already written.
  }
  dynbitset_set(&ctx->typesWritten, typeIndex);

  const XmlNode typeNode = vkgen_entry_find(&ctx->types, key);
  if (!vkgen_write_type_dependencies(ctx, typeNode)) {
    return false;
  }
  const StringHash catHash = xml_attr_get_hash(ctx->schemaDoc, typeNode, g_hash_category);
  if (!catHash) {
    return true; // Primitive type.
  }
  if (catHash == g_hash_basetype || catHash == g_hash_bitmask || catHash == g_hash_funcpointer ||
      catHash == g_hash_define || catHash == g_hash_handle) {
    vkgen_write_node(ctx, typeNode);
    fmt_write(&ctx->out, "\n\n");
    return true;
  }
  if (catHash == g_hash_include) {
    return vkgen_write_include(ctx, key);
  }
  if (catHash == g_hash_enum) {
    return vkgen_write_enum(ctx, xml_attr_get_hash(ctx->schemaDoc, typeNode, g_hash_name));
  }
  if (catHash == g_hash_struct) {
    return vkgen_write_type_struct(ctx, typeNode), true;
  }
  if (catHash == g_hash_union) {
    return vkgen_write_type_union(ctx, typeNode), true;
  }
  return false;
}

static bool vkgen_write_command(VkGenContext* ctx, const StringHash key) {
  const u32 commandIndex = vkgen_entry_index(&ctx->commands, key);
  if (sentinel_check(commandIndex)) {
    return false; // Unknown command.
  }
  if (dynbitset_test(&ctx->commandsWritten, commandIndex)) {
    return true; // Already written.
  }
  dynbitset_set(&ctx->commandsWritten, commandIndex);

  const XmlNode commandNode = vkgen_entry_find(&ctx->commands, key);
  if (!vkgen_write_type_dependencies(ctx, commandNode)) {
    return false;
  }
  const XmlNode protoNode = xml_child_get(ctx->schemaDoc, commandNode, g_hash_proto);
  if (sentinel_check(protoNode)) {
    return false; // Proto node missing (could be an alias).
  }
  const XmlNode protoTypeNode = xml_child_get(ctx->schemaDoc, protoNode, g_hash_type);
  const XmlNode protoNameNode = xml_child_get(ctx->schemaDoc, protoNode, g_hash_name);
  if (sentinel_check(protoTypeNode) || sentinel_check(protoNameNode)) {
    return false; // Name or type missing.
  }
  const String typeStr = xml_child_text(ctx->schemaDoc, protoTypeNode);
  const String nameStr = xml_child_text(ctx->schemaDoc, protoNameNode);
  fmt_write(&ctx->out, "{} SYS_DECL {}(", fmt_text(typeStr), fmt_text(nameStr));
  bool anyParam = false;
  xml_for_children(ctx->schemaDoc, commandNode, child) {
    if (xml_name_hash(ctx->schemaDoc, child) != g_hash_param) {
      continue; // Not a parameter.
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

static bool vkgen_write_extension(VkGenContext* ctx, const StringHash key) {
  const u32 extensionIndex = vkgen_entry_index(&ctx->extensions, key);
  if (sentinel_check(extensionIndex)) {
    return false; // Unknown extension.
  }
  if (dynbitset_test(&ctx->extensionsWritten, extensionIndex)) {
    return true; // Already written.
  }
  dynbitset_set(&ctx->extensionsWritten, extensionIndex);

  const XmlNode node = vkgen_entry_find(&ctx->extensions, key);
  return vkgen_write_requirements(ctx, node);
}

static bool vkgen_write_feature(VkGenContext* ctx, const StringHash key) {
  const u32 featureIndex = vkgen_entry_index(&ctx->features, key);
  if (sentinel_check(featureIndex)) {
    return false; // Unknown feature.
  }
  if (dynbitset_test(&ctx->featuresWritten, featureIndex)) {
    return true; // Already written.
  }
  dynbitset_set(&ctx->featuresWritten, featureIndex);

  const XmlNode node = vkgen_entry_find(&ctx->features, key);
  return vkgen_write_requirements(ctx, node);
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
    vkgen_write_comment_elem(ctx, copyrightElem);
  }

  fmt_write(&ctx->out, "// clang-format off\n\n");
  fmt_write(&ctx->out, "#include \"core.h\"\n\n");

  if (!vkgen_write_enum(ctx, string_hash_lit("API Constants"))) {
    return false;
  }
  if (!vkgen_write_feature(ctx, string_hash_lit("VK_VERSION_1_0"))) {
    return false;
  }
  if (!vkgen_write_feature(ctx, string_hash_lit("VK_VERSION_1_1"))) {
    return false;
  }
  if (!vkgen_write_extension(ctx, string_hash_lit("VK_KHR_surface"))) {
    return false;
  }
  if (!vkgen_write_extension(ctx, string_hash_lit("VK_EXT_validation_features"))) {
    return false;
  }
  if (!vkgen_write_extension(ctx, string_hash_lit("VK_EXT_debug_utils"))) {
    return false;
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
      .schemaDoc         = xml_create(g_allocHeap, 128 * 1024),
      .schemaHost        = cli_read_string(invoc, g_optSchemaHost, g_schemaDefaultHost),
      .schemaUri         = cli_read_string(invoc, g_optSchemaUri, g_schemaDefaultUri),
      .types             = dynarray_create_t(g_allocHeap, VkGenEntry, 4096),
      .typesWritten      = dynbitset_create(g_allocHeap, 4096),
      .enums             = dynarray_create_t(g_allocHeap, VkGenEntry, 512),
      .additions         = dynarray_create_t(g_allocHeap, VkGenAddition, 2048),
      .commands          = dynarray_create_t(g_allocHeap, VkGenEntry, 1024),
      .commandsWritten   = dynbitset_create(g_allocHeap, 1024),
      .extensions        = dynarray_create_t(g_allocHeap, VkGenEntry, 512),
      .extensionsWritten = dynbitset_create(g_allocHeap, 512),
      .features          = dynarray_create_t(g_allocHeap, VkGenEntry, 16),
      .featuresWritten   = dynbitset_create(g_allocHeap, 16),
      .out               = dynstring_create(g_allocHeap, usize_kibibyte * 16),
  };

  ctx.schemaRoot = vkgen_schema_get(ctx.schemaDoc, ctx.schemaHost, ctx.schemaUri);
  if (sentinel_check(ctx.schemaRoot)) {
    goto Exit;
  }

  vkgen_collect_types(&ctx);
  vkgen_collect_enums(&ctx);
  vkgen_collect_commands(&ctx);
  vkgen_collect_extensions(&ctx);
  vkgen_collect_features(&ctx);
  vkgen_collect_additions(&ctx);

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
  dynarray_destroy(&ctx.enums);
  dynarray_destroy(&ctx.additions);
  dynarray_destroy(&ctx.commands);
  dynbitset_destroy(&ctx.commandsWritten);
  dynarray_destroy(&ctx.extensions);
  dynbitset_destroy(&ctx.extensionsWritten);
  dynarray_destroy(&ctx.features);
  dynbitset_destroy(&ctx.featuresWritten);
  dynstring_destroy(&ctx.out);

  net_teardown();
  return success ? 0 : 1;
}
