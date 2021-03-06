#define INCLUDE_SUBDOC_NTOHLL
#include "subdoc-tests-common.h"

using std::string;
using std::cerr;
using std::endl;

class OpTests : public ::testing::Test {};

static string
getNewDoc(const subdoc_OPERATION* op)
{
    string ret;
    for (size_t ii = 0; ii < op->doc_new_len; ii++) {
        const subdoc_LOC *loc = &op->doc_new[ii];
        ret.append(loc->at, loc->length);
    }

    // validate
    jsonsl_error_t rv = subdoc_validate(
        ret.c_str(), ret.size(), op->jsn, SUBDOC_VALIDATE_PARENT_NONE);
    EXPECT_EQ(JSONSL_ERROR_SUCCESS, rv) << t_subdoc::getJsnErrstr(rv);
    return ret;
}

static void
getAssignNewDoc(subdoc_OPERATION *op, string& newdoc)
{
    newdoc = getNewDoc(op);
    SUBDOC_OP_SETDOC(op, newdoc.c_str(), newdoc.size());
}

static subdoc_ERRORS
performNewOp(subdoc_OPERATION *op, subdoc_OPTYPE opcode, const char *path, const char *value = NULL, size_t nvalue = 0)
{
    subdoc_op_clear(op);
    if (value != NULL) {
        if (nvalue == 0) {
            nvalue = strlen(value);
        }
        SUBDOC_OP_SETVALUE(op, value, nvalue);
    }
    SUBDOC_OP_SETCODE(op, opcode);
    return subdoc_op_exec(op, path, strlen(path));
}

static subdoc_ERRORS
performArith(subdoc_OPERATION *op, subdoc_OPTYPE opcode, const char *path, uint64_t delta)
{
    uint64_t ntmp = htonll(delta);
    return performNewOp(op, opcode, path, (const char *)&ntmp, sizeof ntmp);
}

#include "big_json.inc.h"
TEST_F(OpTests, testOperations)
{
    subdoc_OPERATION *op = subdoc_op_alloc();
    string newdoc;

    SUBDOC_OP_SETDOC(op, SAMPLE_big_json, strlen(SAMPLE_big_json));
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, performNewOp(op, SUBDOC_CMD_GET, "name"));
    ASSERT_EQ("\"Allagash Brewing\"", t_subdoc::getMatchString(op->match));
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, performNewOp(op, SUBDOC_CMD_EXISTS, "name"));

    subdoc_ERRORS rv = performNewOp(op, SUBDOC_CMD_DELETE, "address");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);

    getAssignNewDoc(op, newdoc);
    // Should return in KEY_ENOENT
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, performNewOp(op, SUBDOC_CMD_GET, "address"));

    // Insert something back, maybe :)
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD, "address", "\"123 Main St.\"");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);

    getAssignNewDoc(op, newdoc);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, performNewOp(op, SUBDOC_CMD_GET, "address"));
    ASSERT_EQ("\"123 Main St.\"", t_subdoc::getMatchString(op->match));

    // Replace the value now:
    rv = performNewOp(op, SUBDOC_CMD_REPLACE, "address", "\"33 Marginal Rd.\"");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, newdoc);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, performNewOp(op, SUBDOC_CMD_GET, "address"));
    ASSERT_EQ("\"33 Marginal Rd.\"", t_subdoc::getMatchString(op->match));

    // Get it back:
    SUBDOC_OP_SETDOC(op, SAMPLE_big_json, strlen(SAMPLE_big_json));
    // add non-existent path
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD, "foo.bar.baz", "[1,2,3]");
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "foo.bar.baz", "[1,2,3]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getNewDoc(op);

    subdoc_op_free(op);
}

// Mainly checks that we can perform generic DELETE and GET operations
// on array indices
TEST_F(OpTests, testGenericOps)
{
    subdoc_OPERATION *op = subdoc_op_alloc();
    SUBDOC_OP_SETDOC(op, SAMPLE_big_json, strlen(SAMPLE_big_json));
    subdoc_ERRORS rv;
    string newdoc;

    rv = performNewOp(op, SUBDOC_CMD_DELETE, "address[0]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);

    getAssignNewDoc(op, newdoc);
    rv = performNewOp(op, SUBDOC_CMD_GET, "address[0]");
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, rv);

    rv = performNewOp(op, SUBDOC_CMD_REPLACE, "address",
        "[\"500 B St.\", \"Anytown\", \"USA\"]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, newdoc);
    rv = performNewOp(op, SUBDOC_CMD_GET, "address[2]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("\"USA\"", t_subdoc::getMatchString(op->match));
    subdoc_op_free(op);
}

TEST_F(OpTests, testListOps)
{
    string doc = "{}";
    subdoc_OPERATION *op = subdoc_op_alloc();
    SUBDOC_OP_SETDOC(op, doc.c_str(), doc.size());

    subdoc_ERRORS rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "array", "[]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    // Test append:
    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "array", "1");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[0]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("1", t_subdoc::getMatchString(op->match));

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_PREPEND, "array", "0");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[0]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("0", t_subdoc::getMatchString(op->match));
    rv = performNewOp(op, SUBDOC_CMD_GET, "array[1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("1", t_subdoc::getMatchString(op->match));

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "array", "2");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[2]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("2", t_subdoc::getMatchString(op->match));

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "array", "{\"foo\":\"bar\"}");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[3].foo");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("\"bar\"", t_subdoc::getMatchString(op->match));

    // Test the various POP commands
    rv = performNewOp(op, SUBDOC_CMD_DELETE, "array[0]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("0", t_subdoc::getMatchString(op->match));
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[0]");

    rv = performNewOp(op, SUBDOC_CMD_DELETE, "array[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("{\"foo\":\"bar\"}", t_subdoc::getMatchString(op->match));
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_DELETE, "array[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("2", t_subdoc::getMatchString(op->match));


    subdoc_op_free(op);
}

TEST_F(OpTests, testUnique)
{
    string json = "{}";
    string doc;
    subdoc_ERRORS rv;
    subdoc_OPERATION *op = subdoc_op_alloc();
    SUBDOC_OP_SETDOC(op, json.c_str(), json.size());

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE_P, "unique", "\"value\"");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "\"value\"");
    ASSERT_EQ(SUBDOC_STATUS_DOC_EEXISTS, rv);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "1");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "\"1\"");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "[]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "2");
    ASSERT_EQ(SUBDOC_STATUS_PATH_MISMATCH, rv);

    subdoc_op_free(op);
}

#ifndef INT64_MIN
#define INT64_MIN (-9223372036854775807LL-1)
#define INT64_MAX 9223372036854775807LL
#endif

TEST_F(OpTests, testNumeric)
{
    string doc = "{}";
    subdoc_ERRORS rv;
    subdoc_OPERATION *op = subdoc_op_alloc();
    SUBDOC_OP_SETDOC(op, doc.c_str(), doc.size());

    // Can we make a simple counter?
    rv = performArith(op, SUBDOC_CMD_INCREMENT_P, "counter", 1);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_DECREMENT, "counter", 101);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("-100", t_subdoc::getMatchString(op->match));
    getAssignNewDoc(op, doc);

    // Get it raw
    rv = performNewOp(op, SUBDOC_CMD_GET, "counter");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("-100", t_subdoc::getMatchString(op->match));

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "counter", 1);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("-99", t_subdoc::getMatchString(op->match));
    getAssignNewDoc(op, doc);

    // Try with other things
    rv = performArith(op, SUBDOC_CMD_DECREMENT, "counter", INT64_MIN);
    ASSERT_EQ(SUBDOC_STATUS_DELTA_E2BIG, rv);

    rv = performArith(op, SUBDOC_CMD_DECREMENT, "counter", INT64_MAX-99);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "counter", INT64_MAX);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("0", t_subdoc::getMatchString(op->match));
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "counter2", "9999999999999999999999999999999");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "counter2", 1);
    ASSERT_EQ(SUBDOC_STATUS_NUM_E2BIG, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "counter3", "3.14");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "counter3", 1);
    ASSERT_EQ(SUBDOC_STATUS_PATH_MISMATCH, rv);

    doc = "[]";
    SUBDOC_OP_SETDOC(op, doc.c_str(), doc.size());
    rv = performArith(op, SUBDOC_CMD_INCREMENT, "[0]", 42);
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, rv);

    // Try with a _P variant. Should still be the same
    rv = performArith(op, SUBDOC_CMD_INCREMENT_P, "[0]", 42);
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, rv);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "", "-20");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "[0]", 1);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("-19", t_subdoc::getMatchString(op->match));

    subdoc_op_free(op);
}

TEST_F(OpTests, testValueValidation)
{
    subdoc_OPERATION *op = subdoc_op_alloc();
    string json = "{}";
    string doc;
    subdoc_ERRORS rv;
    SUBDOC_OP_SETDOC(op, doc.c_str(), doc.size());

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "foo.bar.baz", "INVALID");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "foo.bar.baz", "1,2,3,4");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // FIXME: Should we allow this? Could be more performant, but might also
    // be confusing!
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "foo.bar.baz", "1,\"k2\":2");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);

    subdoc_op_free(op);
}


TEST_F(OpTests, testNegativeIndex)
{
    subdoc_OPERATION *op = subdoc_op_alloc();
    string json = "[1,2,3,4,5,6]";
    SUBDOC_OP_SETDOC(op, json.c_str(), json.size());

    subdoc_ERRORS rv = performNewOp(op, SUBDOC_CMD_GET, "[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("6", t_subdoc::getMatchString(op->match));

    json = "[1,2,3,[4,5,6,[7,8,9]]]";
    SUBDOC_OP_SETDOC(op, json.c_str(), json.size());
    rv = performNewOp(op, SUBDOC_CMD_GET, "[-1].[-1].[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("9", t_subdoc::getMatchString(op->match));

    string doc;
    rv = performNewOp(op, SUBDOC_CMD_DELETE, "[-1].[-1].[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    // Can we PUSH values with a negative index?
    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "[-1].[-1]", "10");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);


    rv = performNewOp(op, SUBDOC_CMD_GET, "[-1].[-1].[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("10", t_subdoc::getMatchString(op->match));

    // Intermixed paths:
    json = "{\"k1\": [\"first\", {\"k2\":[6,7,8]},\"last\"] }";
    SUBDOC_OP_SETDOC(op, json.c_str(), json.size());

    rv = performNewOp(op, SUBDOC_CMD_GET, "k1[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("\"last\"", t_subdoc::getMatchString(op->match));

    rv = performNewOp(op, SUBDOC_CMD_GET, "k1[1].k2[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("8", t_subdoc::getMatchString(op->match));

    subdoc_op_free(op);
}

TEST_F(OpTests, testRootOps)
{
    subdoc_OPERATION *op = subdoc_op_alloc();
    string json = "[]";
    SUBDOC_OP_SETDOC(op, json.c_str(), json.size());
    subdoc_ERRORS rv;

    rv = performNewOp(op, SUBDOC_CMD_GET, "");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("[]", t_subdoc::getMatchString(op->match));

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "", "null");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, json);

    rv = performNewOp(op, SUBDOC_CMD_GET, "");
    ASSERT_EQ("[null]", t_subdoc::getMatchString(op->match));

    // Deleting root element should be CANTINSERT
    rv = performNewOp(op, SUBDOC_CMD_DELETE, "");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    subdoc_op_free(op);
}

TEST_F(OpTests, testMismatch)
{
    subdoc_OPERATION *op = subdoc_op_alloc();
    string doc = "{}";
    SUBDOC_OP_SETDOC(op, doc.c_str(), doc.size());
    subdoc_ERRORS rv;

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "", "null");
    ASSERT_EQ(SUBDOC_STATUS_PATH_MISMATCH, rv);

    doc = "[]";
    SUBDOC_OP_SETDOC(op, doc.c_str(), doc.size());
    rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "", "blah");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "key", "blah");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    doc = "[null]";
    SUBDOC_OP_SETDOC(op, doc.c_str(), doc.size());
    rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "", "blah");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "key", "blah");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND_P, "foo.bar", "null");
    ASSERT_EQ(SUBDOC_STATUS_PATH_MISMATCH, rv);

    subdoc_op_free(op);
}

TEST_F(OpTests, testWhitespace)
{
    subdoc_OPERATION *op = subdoc_op_alloc();
    string doc = "[ 1, 2, 3,       4        ]";
    SUBDOC_OP_SETDOC(op, doc.c_str(), doc.size());
    subdoc_ERRORS rv;

    rv = performNewOp(op, SUBDOC_CMD_GET, "[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("4", t_subdoc::getMatchString(op->match));
    subdoc_op_free(op);
}
