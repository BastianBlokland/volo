#include "check_spec.h"
#include "core_alloc.h"
#include "data_treescheme.h"

spec(treescheme) {

  typedef struct sTreeNode TreeNode;

  typedef enum {
    TestEnum_A = -42,
    TestEnum_B = 42,
    TestEnum_C = 1337,
  } TestEnum;

  typedef struct {
    String    valString;
    u32       valInt;
    TestEnum  valEnum;
    TreeNode* child;
  } TreeNodeA;

  typedef struct {
    String valString;
    struct {
      TreeNode* values;
      usize     count;
    } children;
  } TreeNodeB;

  typedef struct {
    String   valString;
    TestEnum valEnum;
    struct {
      f32*  values;
      usize count;
    } valFloats;
  } TreeNodeC;

  typedef enum {
    TreeNodeType_A,
    TreeNodeType_B,
    TreeNodeType_C,
    TreeNodeType_D,
  } TreeNodeType;

  typedef struct sTreeNode {
    TreeNodeType type;
    union {
      TreeNodeA data_a;
      TreeNodeB data_b;
      TreeNodeC data_c;
    };
  } TreeNode;

  DataReg* reg      = null;
  DataType nodeType = 0;

  setup() {
    reg = data_reg_create(g_alloc_heap);

    data_reg_enum_t(reg, TestEnum);
    data_reg_const_t(reg, TestEnum, A);
    data_reg_const_t(reg, TestEnum, B);
    data_reg_const_t(reg, TestEnum, C);

    nodeType = data_declare_t(reg, TreeNode);

    data_reg_struct_t(reg, TreeNodeA);
    data_reg_field_t(reg, TreeNodeA, valString, data_prim_t(String));
    data_reg_field_t(reg, TreeNodeA, valInt, data_prim_t(u32));
    data_reg_field_t(reg, TreeNodeA, valEnum, t_TestEnum);
    data_reg_field_t(reg, TreeNodeA, child, nodeType, .container = DataContainer_Pointer);

    data_reg_struct_t(reg, TreeNodeB);
    data_reg_field_t(reg, TreeNodeB, valString, data_prim_t(String));
    data_reg_field_t(reg, TreeNodeB, children, nodeType, .container = DataContainer_Array);

    data_reg_struct_t(reg, TreeNodeC);
    data_reg_field_t(reg, TreeNodeC, valString, data_prim_t(String));
    data_reg_field_t(reg, TreeNodeC, valEnum, t_TestEnum);
    data_reg_field_t(reg, TreeNodeC, valFloats, data_prim_t(f32), .container = DataContainer_Array);
    data_reg_comment_t(reg, TreeNodeC, "Hello Node C");

    data_reg_union_t(reg, TreeNode, type);
    data_reg_choice_t(reg, TreeNode, TreeNodeType_A, data_a, t_TreeNodeA);
    data_reg_choice_t(reg, TreeNode, TreeNodeType_B, data_b, t_TreeNodeB);
    data_reg_choice_t(reg, TreeNode, TreeNodeType_C, data_c, t_TreeNodeC);
    data_reg_choice_empty(reg, TreeNode, TreeNodeType_D);
  }

  it("can write a treescheme file") {
    Mem       buffer    = mem_stack(2 * usize_kibibyte);
    DynString dynString = dynstring_create_over(buffer);
    data_treescheme_write(reg, &dynString, nodeType);

    check_eq_string(
        dynstring_view(&dynString),
        string_lit("{\n"
                   "  \"aliases\": [\n"
                   "    {\n"
                   "      \"identifier\": \"TreeNode\",\n"
                   "      \"values\": [\n"
                   "        \"TreeNodeType_A\",\n"
                   "        \"TreeNodeType_B\",\n"
                   "        \"TreeNodeType_C\",\n"
                   "        \"TreeNodeType_D\"\n"
                   "      ]\n"
                   "    }\n"
                   "  ],\n"
                   "  \"enums\": [\n"
                   "    {\n"
                   "      \"identifier\": \"TestEnum\",\n"
                   "      \"values\": [\n"
                   "        {\n"
                   "          \"value\": -42,\n"
                   "          \"name\": \"A\"\n"
                   "        },\n"
                   "        {\n"
                   "          \"value\": 42,\n"
                   "          \"name\": \"B\"\n"
                   "        },\n"
                   "        {\n"
                   "          \"value\": 1337,\n"
                   "          \"name\": \"C\"\n"
                   "        }\n"
                   "      ]\n"
                   "    }\n"
                   "  ],\n"
                   "  \"nodes\": [\n"
                   "    {\n"
                   "      \"nodeType\": \"TreeNodeType_A\",\n"
                   "      \"fields\": [\n"
                   "        {\n"
                   "          \"name\": \"valString\",\n"
                   "          \"valueType\": \"string\"\n"
                   "        },\n"
                   "        {\n"
                   "          \"name\": \"valInt\",\n"
                   "          \"valueType\": \"number\"\n"
                   "        },\n"
                   "        {\n"
                   "          \"name\": \"valEnum\",\n"
                   "          \"valueType\": \"TestEnum\"\n"
                   "        },\n"
                   "        {\n"
                   "          \"name\": \"child\",\n"
                   "          \"valueType\": \"TreeNode\"\n"
                   "        }\n"
                   "      ]\n"
                   "    },\n"
                   "    {\n"
                   "      \"nodeType\": \"TreeNodeType_B\",\n"
                   "      \"fields\": [\n"
                   "        {\n"
                   "          \"name\": \"valString\",\n"
                   "          \"valueType\": \"string\"\n"
                   "        },\n"
                   "        {\n"
                   "          \"name\": \"children\",\n"
                   "          \"isArray\": true,\n"
                   "          \"valueType\": \"TreeNode\"\n"
                   "        }\n"
                   "      ]\n"
                   "    },\n"
                   "    {\n"
                   "      \"nodeType\": \"TreeNodeType_C\",\n"
                   "      \"comment\": \"Hello Node C\",\n"
                   "      \"fields\": [\n"
                   "        {\n"
                   "          \"name\": \"valString\",\n"
                   "          \"valueType\": \"string\"\n"
                   "        },\n"
                   "        {\n"
                   "          \"name\": \"valEnum\",\n"
                   "          \"valueType\": \"TestEnum\"\n"
                   "        },\n"
                   "        {\n"
                   "          \"name\": \"valFloats\",\n"
                   "          \"isArray\": true,\n"
                   "          \"valueType\": \"number\"\n"
                   "        }\n"
                   "      ]\n"
                   "    },\n"
                   "    {\n"
                   "      \"nodeType\": \"TreeNodeType_D\",\n"
                   "      \"fields\": []\n"
                   "    }\n"
                   "  ],\n"
                   "  \"rootAlias\": \"TreeNode\",\n"
                   "  \"featureNodeNames\": true\n"
                   "}"));
  }

  teardown() { data_reg_destroy(reg); }
}
