#include "app/cli.h"
#include "cli/app.h"
#include "cli/parse.h"
#include "cli/read.h"
#include "core/alloc.h"
#include "core/ascii.h"
#include "core/dynarray.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/format.h"
#include "core/path.h"
#include "log/logger.h"
#include "log/sink_json.h"
#include "log/sink_pretty.h"
#include "xml/doc.h"
#include "xml/read.h"

/**
 * WaylandGen - Utility to generate Wayland protocol headers from XML schema files.
 *
 * Takes one or more Wayland protocol XML files and generates a single header (.h) and
 * implementation (.c) file containing:
 *   - Forward struct declarations for all interfaces.
 *   - Extern wl_interface object declarations.
 *   - Enums for each interface's enum definitions.
 *   - Request opcode constants (#defines) for each interface.
 *   - Listener structs (event vtables) for each interface.
 *   - Minimal wl_interface definitions (name + version only).
 *
 * The generated code uses project-native types (u32, i32) and has no
 * dependency on system wayland-client.h or wayland-util.h.
 */

#define WLGEN_VISIT_HASHES                                                                         \
  WLGEN_HASH(arg)                                                                                  \
  WLGEN_HASH(bitfield)                                                                             \
  WLGEN_HASH(copyright)                                                                            \
  WLGEN_HASH(entry)                                                                                \
  WLGEN_HASH(enum)                                                                                 \
  WLGEN_HASH(event)                                                                                \
  WLGEN_HASH(interface)                                                                            \
  WLGEN_HASH(name)                                                                                 \
  WLGEN_HASH(protocol)                                                                             \
  WLGEN_HASH(request)                                                                              \
  WLGEN_HASH(since)                                                                                \
  WLGEN_HASH(summary)                                                                              \
  WLGEN_HASH(type)                                                                                 \
  WLGEN_HASH(value)                                                                                \
  WLGEN_HASH(version)

static StringHash g_hash_allow_null;

#define WLGEN_HASH(_N_) static StringHash g_hash_##_N_;
WLGEN_VISIT_HASHES
#undef WLGEN_HASH

static void wlgen_init_hashes(void) {
#define WLGEN_HASH(_N_) g_hash_##_N_ = string_hash_lit(#_N_);
  WLGEN_VISIT_HASHES
#undef WLGEN_HASH
  g_hash_allow_null = string_hash_lit("allow-null");
}

// Arg types as found in the XML.
static const String g_wlArgTypeInt    = string_static("int");
static const String g_wlArgTypeUint   = string_static("uint");
static const String g_wlArgTypeFixed  = string_static("fixed");
static const String g_wlArgTypeString = string_static("string");
static const String g_wlArgTypeObject = string_static("object");
static const String g_wlArgTypeNewId  = string_static("new_id");
static const String g_wlArgTypeArray  = string_static("array");
static const String g_wlArgTypeFd     = string_static("fd");

typedef struct {
  String  name;    // protocol name attribute.
  String  path;    // source XML file path (for the generated comment).
  XmlDoc* doc;
  XmlNode root;    // <protocol> node.
} WlGenProtocol;

typedef struct {
  DynArray  protocols; // WlGenProtocol[]
  DynString out;
  String    outName;
} WlGenContext;

// ----- Helpers -----

// Wayland protocol XML occasionally has multi-line attribute values (e.g. summary="...\n  ...").
// The XML lexer terminates strings at newlines, so we normalize newlines inside double-quoted
// attribute values to spaces. We only track double-quotes since all protocol XML uses them for
// attributes; single-quotes in content (apostrophes) would confuse single-quote tracking.
static void wlgen_normalize_attr_newlines(DynString* data) {
  u8*  ptr      = data->data.ptr;
  bool inDouble = false;
  for (usize i = 0; i != data->size; ++i) {
    const u8 ch = ptr[i];
    if (ch == '"') {
      inDouble = !inDouble;
    } else if (inDouble && (ch == '\n' || ch == '\r')) {
      ptr[i] = ' ';
    }
  }
}

static i64 wlgen_parse_int(const String str) {
  i64 result = 0;
  if (string_starts_with(str, string_lit("0x")) || string_starts_with(str, string_lit("0X"))) {
    format_read_i64(string_consume(str, 2), &result, 16);
  } else {
    format_read_i64(str, &result, 10);
  }
  return result;
}

static void wlgen_write_upper(DynString* out, const String str) {
  for (usize i = 0; i != str.size; ++i) {
    dynstring_append_char(out, (u8)ascii_to_upper(*string_at(str, i)));
  }
}

// Write an UPPER_CASE opcode/enum name: <prefix>_<suffix> where both are uppercased.
static void wlgen_write_upper2(DynString* out, const String prefix, const String suffix) {
  wlgen_write_upper(out, prefix);
  dynstring_append_char(out, '_');
  wlgen_write_upper(out, suffix);
}

// Write an UPPER_CASE enum entry name: <prefix>_<middle>_<suffix>.
static void wlgen_write_upper3(
    DynString* out, const String prefix, const String middle, const String suffix) {
  wlgen_write_upper(out, prefix);
  dynstring_append_char(out, '_');
  wlgen_write_upper(out, middle);
  dynstring_append_char(out, '_');
  wlgen_write_upper(out, suffix);
}

// Write the C type for a Wayland arg node.
static void wlgen_write_arg_type(WlGenContext* ctx, XmlDoc* doc, const XmlNode argNode) {
  const String type  = xml_attr_get(doc, argNode, g_hash_type);
  const String iface = xml_attr_get(doc, argNode, g_hash_interface);

  if (string_eq(type, g_wlArgTypeFixed)) {
    fmt_write(&ctx->out, "WlFixed");
  } else if (string_eq(type, g_wlArgTypeInt) || string_eq(type, g_wlArgTypeFd)) {
    fmt_write(&ctx->out, "i32");
  } else if (string_eq(type, g_wlArgTypeUint)) {
    fmt_write(&ctx->out, "u32");
  } else if (string_eq(type, g_wlArgTypeString)) {
    fmt_write(&ctx->out, "const char*");
  } else if (string_eq(type, g_wlArgTypeArray)) {
    fmt_write(&ctx->out, "struct wl_array*");
  } else if (string_eq(type, g_wlArgTypeObject) || string_eq(type, g_wlArgTypeNewId)) {
    if (!string_is_empty(iface)) {
      fmt_write(&ctx->out, "struct {}*", fmt_text(iface));
    } else {
      fmt_write(&ctx->out, "void*");
    }
  } else {
    fmt_write(&ctx->out, "void*"); // Unknown type, fallback.
    log_w("Unknown arg type", log_param("type", fmt_text(type)));
  }
}

// ----- Load -----

static bool wlgen_load_protocol(WlGenContext* ctx, const String path) {
  File*      file = null;
  DynString  buffer;
  bool       success = false;
  XmlDoc*    doc     = null;
  XmlResult  xmlRes;

  const FileResult openRes =
      file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file);
  if (openRes != FileResult_Success) {
    log_e(
        "Failed to open schema",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(file_result_str(openRes))));
    goto Ret;
  }

  buffer = dynstring_create(g_allocHeap, 64 * usize_kibibyte);
  if (file_read_to_end_sync(file, &buffer) != FileResult_Success) {
    log_e("Failed to read schema", log_param("path", fmt_path(path)));
    goto Ret;
  }
  wlgen_normalize_attr_newlines(&buffer);

  doc = xml_create(g_allocHeap, 4096);
  xml_read(doc, dynstring_view(&buffer), &xmlRes);
  dynstring_destroy(&buffer);

  if (xmlRes.type != XmlResultType_Success) {
    log_e(
        "Failed to parse schema",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(xml_error_str(xmlRes.error))));
    goto Ret;
  }

  const XmlNode root = xmlRes.node;
  if (xml_name_hash(doc, root) != g_hash_protocol) {
    log_e("Schema root is not a <protocol> element", log_param("path", fmt_path(path)));
    goto Ret;
  }

  const String protoName = xml_attr_get(doc, root, g_hash_name);
  log_i(
      "Loaded protocol",
      log_param("name", fmt_text(protoName)),
      log_param("path", fmt_path(path)));

  *dynarray_push_t(&ctx->protocols, WlGenProtocol) = (WlGenProtocol){
      .name = protoName,
      .path = path,
      .doc  = doc,
      .root = root,
  };
  doc     = null; // Ownership transferred to protocols array.
  success = true;

Ret:
  if (doc) {
    xml_destroy(doc);
  }
  if (file) {
    file_destroy(file);
  }
  return success;
}

// ----- Write helpers -----

static void wlgen_write_prolog(WlGenContext* ctx) {
  fmt_write(&ctx->out, "// Generated by 'wlgen' with the following protocols:\n");
  dynarray_for_t(&ctx->protocols, WlGenProtocol, proto) {
    fmt_write(&ctx->out, "// - {}\n", fmt_text(path_filename(proto->path)));
    const XmlNode copyrightNode = xml_child_get(proto->doc, proto->root, g_hash_copyright);
    if (!sentinel_check(copyrightNode)) {
      const XmlNode textNode = xml_first_child(proto->doc, copyrightNode);
      if (!sentinel_check(textNode) && xml_is(proto->doc, textNode, XmlType_Text)) {
        String text = xml_value(proto->doc, textNode);
        while (!string_is_empty(text)) {
          const usize  nl   = string_find_first_char(text, '\n');
          const String line = string_trim_whitespace(sentinel_check(nl) ? text : string_slice(text, 0, nl));
          if (string_starts_with(line, string_lit("Copyright"))) {
            fmt_write(&ctx->out, "//   {}\n", fmt_text(line));
          }
          if (sentinel_check(nl)) {
            break;
          }
          text = string_consume(text, nl + 1);
        }
      }
    }
  }
}

// ----- Write header -----

static void wlgen_write_header_forward_decls(WlGenContext* ctx) {
  dynarray_for_t(&ctx->protocols, WlGenProtocol, proto) {
    xml_for_children(proto->doc, proto->root, ifaceNode) {
      if (xml_name_hash(proto->doc, ifaceNode) != g_hash_interface) {
        continue;
      }
      const String name = xml_attr_get(proto->doc, ifaceNode, g_hash_name);
      // wl_display and wl_registry are already declared in the WlFuncs preamble.
      if (string_eq(name, string_lit("wl_display")) || string_eq(name, string_lit("wl_registry"))) {
        continue;
      }
      fmt_write(&ctx->out, "struct {};\n", fmt_text(name));
    }
  }
  fmt_write(&ctx->out, "\n");
}

static void wlgen_write_header_iface_externs(WlGenContext* ctx) {
  dynarray_for_t(&ctx->protocols, WlGenProtocol, proto) {
    xml_for_children(proto->doc, proto->root, ifaceNode) {
      if (xml_name_hash(proto->doc, ifaceNode) != g_hash_interface) {
        continue;
      }
      const String name = xml_attr_get(proto->doc, ifaceNode, g_hash_name);
      fmt_write(&ctx->out, "extern const struct wl_interface {}_interface;\n", fmt_text(name));
    }
  }
  fmt_write(&ctx->out, "\n");
}

// Scan an interface node for flags used by both header and impl writers.
static void wlgen_iface_scan(
    XmlDoc* doc, const XmlNode ifaceNode, bool* outHasEvents, bool* outHasDestructor) {
  *outHasEvents     = false;
  *outHasDestructor = false;
  xml_for_children(doc, ifaceNode, child) {
    const StringHash h = xml_name_hash(doc, child);
    if (h == g_hash_event) {
      *outHasEvents = true;
    } else if (h == g_hash_request) {
      if (string_eq(xml_attr_get(doc, child, g_hash_type), string_lit("destructor"))) {
        *outHasDestructor = true;
      }
    }
  }
}

// Scan a request node for its new_id arg (if any).
// Returns true if the request has an untyped new_id (e.g. wl_registry.bind) and should be skipped.
static bool wlgen_req_scan_new_id(XmlDoc* doc, const XmlNode reqNode, String* outReturnIface) {
  *outReturnIface = string_empty;
  xml_for_children(doc, reqNode, argNode) {
    if (xml_name_hash(doc, argNode) != g_hash_arg) {
      continue;
    }
    if (!string_eq(xml_attr_get(doc, argNode, g_hash_type), g_wlArgTypeNewId)) {
      continue;
    }
    const String iface = xml_attr_get(doc, argNode, g_hash_interface);
    if (string_is_empty(iface)) {
      return true; // Untyped new_id — caller should skip this request.
    }
    *outReturnIface = iface;
  }
  return false;
}

// Write the header section for one interface: enums, opcode defines, listener struct, declarations.
static void wlgen_write_iface_decls(WlGenContext* ctx, XmlDoc* doc, const XmlNode ifaceNode) {
  const String ifaceName = xml_attr_get(doc, ifaceNode, g_hash_name);

  // Enums.
  xml_for_children(doc, ifaceNode, child) {
    if (xml_name_hash(doc, child) != g_hash_enum) {
      continue;
    }
    const String enumName = xml_attr_get(doc, child, g_hash_name);
    fmt_write(&ctx->out, "enum {}_{} {\n", fmt_text(ifaceName), fmt_text(enumName));
    xml_for_children(doc, child, entryNode) {
      if (xml_name_hash(doc, entryNode) != g_hash_entry) {
        continue;
      }
      const String entryName  = xml_attr_get(doc, entryNode, g_hash_name);
      const String entryValue = xml_attr_get(doc, entryNode, g_hash_value);
      fmt_write(&ctx->out, "  ");
      wlgen_write_upper3(&ctx->out, ifaceName, enumName, entryName);
      fmt_write(&ctx->out, " = {},\n", fmt_int(wlgen_parse_int(entryValue)));
    }
    dynstring_append(&ctx->out, string_lit("};\n\n"));
  }

  // Request opcodes.
  u32 opcode = 0;
  xml_for_children(doc, ifaceNode, child) {
    if (xml_name_hash(doc, child) != g_hash_request) {
      continue;
    }
    dynstring_append(&ctx->out, string_lit("#define "));
    wlgen_write_upper2(&ctx->out, ifaceName, xml_attr_get(doc, child, g_hash_name));
    fmt_write(&ctx->out, " {}\n", fmt_int(opcode++));
  }
  if (opcode) {
    dynstring_append(&ctx->out, string_lit("\n"));
  }

  // Listener struct (from events).
  bool hasEvent = false;
  xml_for_children(doc, ifaceNode, child) {
    if (xml_name_hash(doc, child) != g_hash_event) {
      continue;
    }
    if (!hasEvent) {
      fmt_write(&ctx->out, "struct {}_listener {\n", fmt_text(ifaceName));
      hasEvent = true;
    }
    const String evtName = xml_attr_get(doc, child, g_hash_name);
    fmt_write(&ctx->out, "  void (*{})(void* data, struct {}*", fmt_text(evtName), fmt_text(ifaceName));
    xml_for_children(doc, child, argNode) {
      if (xml_name_hash(doc, argNode) != g_hash_arg) {
        continue;
      }
      fmt_write(&ctx->out, ", ");
      wlgen_write_arg_type(ctx, doc, argNode);
      fmt_write(&ctx->out, " {}", fmt_text(xml_attr_get(doc, argNode, g_hash_name)));
    }
    dynstring_append(&ctx->out, string_lit(");\n"));
  }
  if (hasEvent) {
    dynstring_append(&ctx->out, string_lit("};\n\n"));
  }

  // Utility wrapper declarations: get_version, destroy, add_listener.
  bool hasEvents, hasDestructor;
  wlgen_iface_scan(doc, ifaceNode, &hasEvents, &hasDestructor);
  // wl_display is managed by display_connect/disconnect; proxy helpers are not applicable.
  const bool isDisplay = string_eq(ifaceName, string_lit("wl_display"));

  if (!isDisplay) {
    fmt_write(&ctx->out, "u32 {}_get_version(const WlFuncs*, struct {}*);\n", fmt_text(ifaceName), fmt_text(ifaceName));
  }
  if (!hasDestructor && !isDisplay) {
    fmt_write(&ctx->out, "void {}_destroy(const WlFuncs*, struct {}*);\n", fmt_text(ifaceName), fmt_text(ifaceName));
  }
  if (hasEvents && !isDisplay) {
    fmt_write(&ctx->out, "void {}_add_listener(const WlFuncs*, struct {}*, const struct {}_listener*, void*);\n", fmt_text(ifaceName), fmt_text(ifaceName), fmt_text(ifaceName));
  }

  // Request wrapper declarations.
  xml_for_children(doc, ifaceNode, child) {
    if (xml_name_hash(doc, child) != g_hash_request) {
      continue;
    }
    String returnIface;
    if (wlgen_req_scan_new_id(doc, child, &returnIface)) {
      continue; // Skip untyped new_id (e.g. wl_registry.bind).
    }
    if (!string_is_empty(returnIface)) {
      fmt_write(&ctx->out, "struct {}* ", fmt_text(returnIface));
    } else {
      dynstring_append(&ctx->out, string_lit("void "));
    }
    fmt_write(&ctx->out, "{}_{}", fmt_text(ifaceName), fmt_text(xml_attr_get(doc, child, g_hash_name)));
    fmt_write(&ctx->out, "(const WlFuncs*, struct {}*", fmt_text(ifaceName));
    xml_for_children(doc, child, argNode) {
      if (xml_name_hash(doc, argNode) != g_hash_arg) {
        continue;
      }
      if (string_eq(xml_attr_get(doc, argNode, g_hash_type), g_wlArgTypeNewId)) {
        continue; // New_id becomes the return value.
      }
      fmt_write(&ctx->out, ", ");
      wlgen_write_arg_type(ctx, doc, argNode);
      fmt_write(&ctx->out, " {}", fmt_text(xml_attr_get(doc, argNode, g_hash_name)));
    }
    dynstring_append(&ctx->out, string_lit(");\n"));
  }
  dynstring_append(&ctx->out, string_lit("\n"));
}

// Write the implementation section for one interface: function definitions.
static void wlgen_write_iface_defs(WlGenContext* ctx, XmlDoc* doc, const XmlNode ifaceNode) {
  const String ifaceName = xml_attr_get(doc, ifaceNode, g_hash_name);

  bool hasEvents, hasDestructor;
  wlgen_iface_scan(doc, ifaceNode, &hasEvents, &hasDestructor);
  const bool isDisplay = string_eq(ifaceName, string_lit("wl_display"));

  if (!isDisplay) {
    fmt_write(&ctx->out, "u32 {}_get_version(const WlFuncs* api, struct {}* obj)", fmt_text(ifaceName), fmt_text(ifaceName));
    dynstring_append(&ctx->out, string_lit(" {\n  return api->proxy_get_version((struct wl_proxy*)obj);\n}\n\n"));
  }
  if (!hasDestructor && !isDisplay) {
    fmt_write(&ctx->out, "void {}_destroy(const WlFuncs* api, struct {}* obj)", fmt_text(ifaceName), fmt_text(ifaceName));
    dynstring_append(&ctx->out, string_lit(" {\n  api->proxy_destroy((struct wl_proxy*)obj);\n}\n\n"));
  }
  if (hasEvents && !isDisplay) {
    fmt_write(&ctx->out, "void {}_add_listener(const WlFuncs* api, struct {}* obj, const struct {}_listener* listener, void* data)", fmt_text(ifaceName), fmt_text(ifaceName), fmt_text(ifaceName));
    dynstring_append(&ctx->out, string_lit(" {\n  api->proxy_add_listener((struct wl_proxy*)obj, (void(**)(void))listener, data);\n}\n\n"));
  }

  // Request wrapper definitions.
  xml_for_children(doc, ifaceNode, child) {
    if (xml_name_hash(doc, child) != g_hash_request) {
      continue;
    }
    const String reqName      = xml_attr_get(doc, child, g_hash_name);
    const bool   isDestructor = string_eq(xml_attr_get(doc, child, g_hash_type), string_lit("destructor"));

    String returnIface;
    if (wlgen_req_scan_new_id(doc, child, &returnIface)) {
      continue; // Skip untyped new_id (e.g. wl_registry.bind).
    }

    // Signature.
    if (!string_is_empty(returnIface)) {
      fmt_write(&ctx->out, "struct {}* ", fmt_text(returnIface));
    } else {
      dynstring_append(&ctx->out, string_lit("void "));
    }
    fmt_write(&ctx->out, "{}_{}", fmt_text(ifaceName), fmt_text(reqName));
    fmt_write(&ctx->out, "(const WlFuncs* api, struct {}* obj", fmt_text(ifaceName));
    xml_for_children(doc, child, argNode) {
      if (xml_name_hash(doc, argNode) != g_hash_arg) {
        continue;
      }
      if (string_eq(xml_attr_get(doc, argNode, g_hash_type), g_wlArgTypeNewId)) {
        continue; // New_id becomes the return value.
      }
      fmt_write(&ctx->out, ", ");
      wlgen_write_arg_type(ctx, doc, argNode);
      fmt_write(&ctx->out, " {}", fmt_text(xml_attr_get(doc, argNode, g_hash_name)));
    }

    // Body.
    dynstring_append(&ctx->out, string_lit(") {\n"));
    if (!string_is_empty(returnIface)) {
      dynstring_append(&ctx->out, string_lit("  return api->proxy_marshal_flags(\n"));
    } else {
      dynstring_append(&ctx->out, string_lit("  api->proxy_marshal_flags(\n"));
    }
    dynstring_append(&ctx->out, string_lit("      (struct wl_proxy*)obj, "));
    wlgen_write_upper2(&ctx->out, ifaceName, reqName);
    if (!string_is_empty(returnIface)) {
      fmt_write(&ctx->out, ", &{}_interface", fmt_text(returnIface));
    } else {
      dynstring_append(&ctx->out, string_lit(", null"));
    }
    dynstring_append(&ctx->out, string_lit(",\n      api->proxy_get_version((struct wl_proxy*)obj),\n"));
    if (isDestructor) {
      dynstring_append(&ctx->out, string_lit("      WL_MARSHAL_FLAG_DESTROY"));
    } else {
      dynstring_append(&ctx->out, string_lit("      0"));
    }
    xml_for_children(doc, child, argNode) {
      if (xml_name_hash(doc, argNode) != g_hash_arg) {
        continue;
      }
      if (string_eq(xml_attr_get(doc, argNode, g_hash_type), g_wlArgTypeNewId)) {
        dynstring_append(&ctx->out, string_lit(", null")); // Placeholder for the new proxy.
        continue;
      }
      fmt_write(&ctx->out, ", {}", fmt_text(xml_attr_get(doc, argNode, g_hash_name)));
    }
    dynstring_append(&ctx->out, string_lit(");\n}\n\n"));
  }
}

static void wlgen_write_header_funcs(WlGenContext* ctx) {
  dynstring_append(&ctx->out, string_lit(
      "#define WL_MARSHAL_FLAG_DESTROY (1 << 0)\n\n"
      "struct wl_proxy;\n"
      "struct wl_display;\n"
      "struct wl_registry;\n\n"
      "typedef struct {\n"
      "  struct wl_display*  (SYS_DECL* display_connect)(const char* name);\n"
      "  void                (SYS_DECL* display_disconnect)(struct wl_display*);\n"
      "  int                 (SYS_DECL* display_dispatch)(struct wl_display*);\n"
      "  int                 (SYS_DECL* display_dispatch_pending)(struct wl_display*);\n"
      "  int                 (SYS_DECL* display_roundtrip)(struct wl_display*);\n"
      "  int                 (SYS_DECL* display_flush)(struct wl_display*);\n"
      "  int                 (SYS_DECL* proxy_add_listener)(struct wl_proxy*, void(**)(void), void*);\n"
      "  void*               (SYS_DECL* proxy_marshal_flags)(struct wl_proxy*, u32 opcode, const struct wl_interface*, u32 version, u32 flags, ...);\n"
      "  u32                 (SYS_DECL* proxy_get_version)(struct wl_proxy*);\n"
      "  void                (SYS_DECL* proxy_destroy)(struct wl_proxy*);\n"
      "} WlFuncs;\n\n"
      "bool wlLoad(const DynLib*, WlFuncs*);\n\n"
      "void* wlRegistryBind(const WlFuncs*, struct wl_registry*, u32 name, u32 version, const struct wl_interface*, u32 maxVersion);\n\n"));
}

static void wlgen_write_header(WlGenContext* ctx) {
  fmt_write(&ctx->out, "#pragma once\n");
  fmt_write(&ctx->out, "// clang-format off\n");
  wlgen_write_prolog(ctx);
  fmt_write(&ctx->out, "\n");
  fmt_write(&ctx->out, "#include \"core/forward.h\"\n\n");
  fmt_write(&ctx->out, "struct wl_message {\n");
  fmt_write(&ctx->out, "  const char*                 name;\n");
  fmt_write(&ctx->out, "  const char*                 signature;\n");
  fmt_write(&ctx->out, "  const struct wl_interface** types;\n");
  fmt_write(&ctx->out, "};\n\n");
  fmt_write(&ctx->out, "struct wl_interface {\n");
  fmt_write(&ctx->out, "  const char*              name;\n");
  fmt_write(&ctx->out, "  int                      version;\n");
  fmt_write(&ctx->out, "  int                      method_count;\n");
  fmt_write(&ctx->out, "  const struct wl_message* methods;\n");
  fmt_write(&ctx->out, "  int                      event_count;\n");
  fmt_write(&ctx->out, "  const struct wl_message* events;\n");
  fmt_write(&ctx->out, "};\n\n");
  fmt_write(&ctx->out, "struct wl_array {\n");
  fmt_write(&ctx->out, "  usize size, alloc;\n");
  fmt_write(&ctx->out, "  void* data;\n");
  fmt_write(&ctx->out, "};\n\n");
  fmt_write(&ctx->out, "typedef i32 WlFixed;\n\n");

  wlgen_write_header_funcs(ctx);
  wlgen_write_header_forward_decls(ctx);
  wlgen_write_header_iface_externs(ctx);

  dynarray_for_t(&ctx->protocols, WlGenProtocol, proto) {
    xml_for_children(proto->doc, proto->root, ifaceNode) {
      if (xml_name_hash(proto->doc, ifaceNode) != g_hash_interface) {
        continue;
      }
      wlgen_write_iface_decls(ctx, proto->doc, ifaceNode);
    }
  }
  dynstring_append(&ctx->out, string_lit("// clang-format on\n"));
}

// ----- Write implementation -----

static void wlgen_write_impl_load_sym(WlGenContext* ctx, const char* field, const char* sym) {
  fmt_write(&ctx->out, "  if (!(out->{} = dynlib_symbol(lib, string_lit(\"{}\")))) ", fmt_text(string_from_null_term(field)), fmt_text(string_from_null_term(sym)));
  dynstring_append(&ctx->out, string_lit("{ return false; }\n"));
}

static void wlgen_write_impl_load(WlGenContext* ctx) {
  dynstring_append(&ctx->out, string_lit("bool wlLoad(const DynLib* lib, WlFuncs* out) {\n"));
  wlgen_write_impl_load_sym(ctx, "display_connect",          "wl_display_connect");
  wlgen_write_impl_load_sym(ctx, "display_disconnect",       "wl_display_disconnect");
  wlgen_write_impl_load_sym(ctx, "display_dispatch",         "wl_display_dispatch");
  wlgen_write_impl_load_sym(ctx, "display_dispatch_pending", "wl_display_dispatch_pending");
  wlgen_write_impl_load_sym(ctx, "display_roundtrip",        "wl_display_roundtrip");
  wlgen_write_impl_load_sym(ctx, "display_flush",            "wl_display_flush");
  wlgen_write_impl_load_sym(ctx, "proxy_add_listener",       "wl_proxy_add_listener");
  wlgen_write_impl_load_sym(ctx, "proxy_marshal_flags",      "wl_proxy_marshal_flags");
  wlgen_write_impl_load_sym(ctx, "proxy_get_version",        "wl_proxy_get_version");
  wlgen_write_impl_load_sym(ctx, "proxy_destroy",            "wl_proxy_destroy");
  dynstring_append(&ctx->out, string_lit(
      "  return true;\n}\n\n"
      "void* wlRegistryBind(\n"
      "    const WlFuncs* api, struct wl_registry* registry, u32 name, u32 version,\n"
      "    const struct wl_interface* iface, u32 maxVersion) {\n"
      "  const u32 bindVersion = version < maxVersion ? version : maxVersion;\n"
      "  return api->proxy_marshal_flags(\n"
      "      (struct wl_proxy*)registry, WL_REGISTRY_BIND, iface, bindVersion, 0,\n"
      "      name, iface->name, bindVersion, null);\n"
      "}\n\n"));
}

// Write the signature string characters for a request or event XML node.
static void wlgen_write_message_signature(WlGenContext* ctx, XmlDoc* doc, const XmlNode msgNode) {
  const String since = xml_attr_get(doc, msgNode, g_hash_since);
  if (!string_is_empty(since)) {
    const i64 sinceVer = wlgen_parse_int(since);
    if (sinceVer > 1) {
      fmt_write(&ctx->out, "{}", fmt_int(sinceVer));
    }
  }
  xml_for_children(doc, msgNode, argNode) {
    if (xml_name_hash(doc, argNode) != g_hash_arg) {
      continue;
    }
    const String type      = xml_attr_get(doc, argNode, g_hash_type);
    const String iface     = xml_attr_get(doc, argNode, g_hash_interface);
    const String allowNull = xml_attr_get(doc, argNode, g_hash_allow_null);
    const bool   nullable  = string_eq(allowNull, string_lit("true"));
    if (string_eq(type, g_wlArgTypeInt)) {
      dynstring_append_char(&ctx->out, 'i');
    } else if (string_eq(type, g_wlArgTypeUint)) {
      dynstring_append_char(&ctx->out, 'u');
    } else if (string_eq(type, g_wlArgTypeFixed)) {
      dynstring_append_char(&ctx->out, 'f');
    } else if (string_eq(type, g_wlArgTypeString)) {
      if (nullable) {
        dynstring_append_char(&ctx->out, '?');
      }
      dynstring_append_char(&ctx->out, 's');
    } else if (string_eq(type, g_wlArgTypeObject)) {
      if (nullable) {
        dynstring_append_char(&ctx->out, '?');
      }
      dynstring_append_char(&ctx->out, 'o');
    } else if (string_eq(type, g_wlArgTypeNewId)) {
      if (string_is_empty(iface)) {
        // Untyped new_id: inject string (interface name) + uint (version) before new_id.
        dynstring_append_char(&ctx->out, 's');
        dynstring_append_char(&ctx->out, 'u');
      }
      dynstring_append_char(&ctx->out, 'n');
    } else if (string_eq(type, g_wlArgTypeArray)) {
      dynstring_append_char(&ctx->out, 'a');
    } else if (string_eq(type, g_wlArgTypeFd)) {
      dynstring_append_char(&ctx->out, 'h');
    }
  }
}

// Returns true if this message needs a types[] array (has any object or new_id arg).
static bool wlgen_msg_needs_types(XmlDoc* doc, const XmlNode msgNode) {
  xml_for_children(doc, msgNode, argNode) {
    if (xml_name_hash(doc, argNode) != g_hash_arg) {
      continue;
    }
    const String type = xml_attr_get(doc, argNode, g_hash_type);
    if (string_eq(type, g_wlArgTypeObject) || string_eq(type, g_wlArgTypeNewId)) {
      return true;
    }
  }
  return false;
}

// Write a types[] array for a single message that has object/new_id args.
// The array length and order matches the expanded signature (untyped new_id → 3 slots).
static void wlgen_write_msg_types(
    WlGenContext*  ctx,
    XmlDoc*        doc,
    const String   ifaceName,
    const XmlNode  msgNode) {
  const String msgName = xml_attr_get(doc, msgNode, g_hash_name);
  fmt_write(
      &ctx->out,
      "static const struct wl_interface* {}_{}_types[] = {",
      fmt_text(ifaceName),
      fmt_text(msgName));
  bool firstArg = true;
  xml_for_children(doc, msgNode, argNode) {
    if (xml_name_hash(doc, argNode) != g_hash_arg) {
      continue;
    }
    if (!firstArg) {
      dynstring_append(&ctx->out, string_lit(", "));
    }
    firstArg               = false;
    const String type      = xml_attr_get(doc, argNode, g_hash_type);
    const String iface     = xml_attr_get(doc, argNode, g_hash_interface);
    if (string_eq(type, g_wlArgTypeObject)) {
      if (string_is_empty(iface)) {
        dynstring_append(&ctx->out, string_lit("null"));
      } else {
        fmt_write(&ctx->out, "&{}_interface", fmt_text(iface));
      }
    } else if (string_eq(type, g_wlArgTypeNewId)) {
      if (string_is_empty(iface)) {
        // Untyped new_id expands to s (null) + u (null) + n (null) in the signature.
        dynstring_append(&ctx->out, string_lit("null, null, null"));
      } else {
        fmt_write(&ctx->out, "&{}_interface", fmt_text(iface));
      }
    } else {
      dynstring_append(&ctx->out, string_lit("null"));
    }
  }
  dynstring_append(&ctx->out, string_lit("};\n"));
}

// Write a single static wl_message array (requests or events) for an interface, if non-empty.
static void wlgen_write_msg_array(
    WlGenContext*    ctx,
    XmlDoc*          doc,
    const String     ifaceName,
    const XmlNode    ifaceNode,
    const StringHash msgHash,
    const String     arrayName) {
  bool started = false;
  xml_for_children(doc, ifaceNode, msgNode) {
    if (xml_name_hash(doc, msgNode) != msgHash) {
      continue;
    }
    if (!started) {
      fmt_write(&ctx->out, "static const struct wl_message {}_{}[] = {\n", fmt_text(ifaceName), fmt_text(arrayName));
      started = true;
    }
    const String msgName = xml_attr_get(doc, msgNode, g_hash_name);
    dynstring_append(&ctx->out, string_lit("  {\""));
    dynstring_append(&ctx->out, msgName);
    dynstring_append(&ctx->out, string_lit("\", \""));
    wlgen_write_message_signature(ctx, doc, msgNode);
    dynstring_append(&ctx->out, string_lit("\", "));
    if (wlgen_msg_needs_types(doc, msgNode)) {
      fmt_write(&ctx->out, "{}_{}_types", fmt_text(ifaceName), fmt_text(msgName));
    } else {
      dynstring_append(&ctx->out, string_lit("null"));
    }
    dynstring_append(&ctx->out, string_lit("},\n"));
  }
  if (started) {
    dynstring_append(&ctx->out, string_lit("};\n"));
  }
}

// Write static wl_message arrays for a single interface's requests and events.
static void wlgen_write_impl_iface_messages(WlGenContext* ctx, XmlDoc* doc, const XmlNode ifaceNode) {
  const String ifaceName = xml_attr_get(doc, ifaceNode, g_hash_name);

  // Types arrays for messages with object/new_id args.
  xml_for_children(doc, ifaceNode, msgNode) {
    const StringHash h = xml_name_hash(doc, msgNode);
    if ((h == g_hash_request || h == g_hash_event) && wlgen_msg_needs_types(doc, msgNode)) {
      wlgen_write_msg_types(ctx, doc, ifaceName, msgNode);
    }
  }

  wlgen_write_msg_array(ctx, doc, ifaceName, ifaceNode, g_hash_request, string_lit("requests"));
  wlgen_write_msg_array(ctx, doc, ifaceName, ifaceNode, g_hash_event, string_lit("events"));
}

static void wlgen_write_impl(WlGenContext* ctx) {
  fmt_write(&ctx->out, "// clang-format off\n");
  wlgen_write_prolog(ctx);
  fmt_write(&ctx->out, "\n");
  fmt_write(&ctx->out, "#include \"{}.h\"\n\n", fmt_text(ctx->outName));
  fmt_write(&ctx->out, "#include \"core/dynlib.h\"\n");
  fmt_write(&ctx->out, "#include \"core/string.h\"\n\n");

  wlgen_write_impl_load(ctx);

  // Per-interface message arrays (requests + events with signatures).
  dynarray_for_t(&ctx->protocols, WlGenProtocol, proto) {
    xml_for_children(proto->doc, proto->root, ifaceNode) {
      if (xml_name_hash(proto->doc, ifaceNode) != g_hash_interface) {
        continue;
      }
      wlgen_write_impl_iface_messages(ctx, proto->doc, ifaceNode);
    }
  }
  fmt_write(&ctx->out, "\n");

  // wl_interface definitions.
  dynarray_for_t(&ctx->protocols, WlGenProtocol, proto) {
    xml_for_children(proto->doc, proto->root, ifaceNode) {
      if (xml_name_hash(proto->doc, ifaceNode) != g_hash_interface) {
        continue;
      }
      const String name    = xml_attr_get(proto->doc, ifaceNode, g_hash_name);
      const String verStr  = xml_attr_get(proto->doc, ifaceNode, g_hash_version);
      const i64    version = string_is_empty(verStr) ? 1 : wlgen_parse_int(verStr);

      u32 methodCount = 0, eventCount = 0;
      xml_for_children(proto->doc, ifaceNode, child) {
        const StringHash h = xml_name_hash(proto->doc, child);
        if (h == g_hash_request) {
          ++methodCount;
        } else if (h == g_hash_event) {
          ++eventCount;
        }
      }

      fmt_write(&ctx->out, "const struct wl_interface {}_interface = ", fmt_text(name));
      dynstring_append(&ctx->out, string_lit("{\""));
      dynstring_append(&ctx->out, name);
      fmt_write(&ctx->out, "\", {}, {}, ", fmt_int(version), fmt_int(methodCount));
      if (methodCount) {
        fmt_write(&ctx->out, "{}_requests, ", fmt_text(name));
      } else {
        fmt_write(&ctx->out, "null, ");
      }
      fmt_write(&ctx->out, "{}, ", fmt_int(eventCount));
      if (eventCount) {
        fmt_write(&ctx->out, "{}_events};\n", fmt_text(name));
      } else {
        fmt_write(&ctx->out, "null};\n");
      }
    }
  }
  fmt_write(&ctx->out, "\n");

  dynarray_for_t(&ctx->protocols, WlGenProtocol, proto) {
    xml_for_children(proto->doc, proto->root, ifaceNode) {
      if (xml_name_hash(proto->doc, ifaceNode) != g_hash_interface) {
        continue;
      }
      wlgen_write_iface_defs(ctx, proto->doc, ifaceNode);
    }
  }
  dynstring_append(&ctx->out, string_lit("// clang-format on\n"));
}

// ----- CLI -----

// clang-format off
static const String g_appDesc = string_static("WaylandGen - Utility to generate Wayland protocol headers from XML schema files.");
// clang-format on

static CliId g_optVerbose, g_optOutputPath, g_optSchemas;

AppType app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, g_appDesc);

  g_optVerbose    = cli_register_flag(app, '\0', string_lit("verbose"), CliOptionFlags_None);
  g_optOutputPath = cli_register_arg(app, string_lit("output-path"), CliOptionFlags_Required);
  g_optSchemas    = cli_register_flag(app, 's', string_lit("schema"), CliOptionFlags_RequiredMultiValue);

  cli_register_desc(
      app,
      g_optOutputPath,
      string_lit("Output path for the header and implementation (.h and .c are appended)."));
  cli_register_desc(
      app,
      g_optSchemas,
      string_lit("Path(s) to Wayland protocol XML file(s). Can be specified multiple times."));

  return AppType_Console;
}

i32 app_cli_run(MAYBE_UNUSED const CliApp* app, const CliInvocation* invoc) {
  const LogMask logMask = cli_parse_provided(invoc, g_optVerbose) ? LogMask_All : ~LogMask_Debug;
  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, g_fileStdOut, logMask));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  wlgen_init_hashes();

  bool success = false;

  WlGenContext ctx = {
      .protocols = dynarray_create_t(g_allocHeap, WlGenProtocol, 8),
      .out       = dynstring_create(g_allocHeap, usize_kibibyte * 16),
      .outName   = path_stem(cli_read_string(invoc, g_optOutputPath, string_empty)),
  };

  const String        outputPath = cli_read_string(invoc, g_optOutputPath, string_empty);
  const CliParseValues schemas   = cli_parse_values(invoc, g_optSchemas);

  // Load all protocol XML files.
  for (usize i = 0; i != schemas.count; ++i) {
    if (!wlgen_load_protocol(&ctx, schemas.values[i])) {
      goto Exit;
    }
  }
  if (!ctx.protocols.size) {
    log_e("No protocols loaded");
    goto Exit;
  }

  // Write header.
  wlgen_write_header(&ctx);
  {
    const String headerPath = fmt_write_scratch("{}.h", fmt_text(outputPath));
    if (file_write_to_path_sync(headerPath, dynstring_view(&ctx.out)) == FileResult_Success) {
      log_i("Generated header", log_param("path", fmt_path(headerPath)));
    } else {
      log_e("Failed to write header", log_param("path", fmt_path(headerPath)));
      goto Exit;
    }
  }

  // Write implementation.
  dynstring_clear(&ctx.out);
  wlgen_write_impl(&ctx);
  {
    const String implPath = fmt_write_scratch("{}.c", fmt_text(outputPath));
    if (file_write_to_path_sync(implPath, dynstring_view(&ctx.out)) == FileResult_Success) {
      log_i("Generated implementation", log_param("path", fmt_path(implPath)));
    } else {
      log_e("Failed to write implementation", log_param("path", fmt_path(implPath)));
      goto Exit;
    }
  }
  success = true;

Exit:
  dynarray_for_t(&ctx.protocols, WlGenProtocol, proto) { xml_destroy(proto->doc); }
  dynarray_destroy(&ctx.protocols);
  dynstring_destroy(&ctx.out);
  return success ? 0 : 1;
}
