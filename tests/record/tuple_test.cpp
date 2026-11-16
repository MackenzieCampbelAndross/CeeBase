#include <gtest/gtest.h>

#include "nexus/record/schema.hpp"
#include "nexus/record/tuple.hpp"
#include "nexus/record/value.hpp"

namespace nexus {
namespace {

TEST(TupleTest, SerializationRoundTrip) {
    std::vector<Column> cols;
    cols.emplace_back("id", DataType::INTEGER, false);
    cols.emplace_back("name", DataType::TEXT, false);
    cols.emplace_back("score", DataType::DOUBLE, true);
    cols.emplace_back("is_active", DataType::BOOLEAN, false);
    cols.emplace_back("notes", DataType::TEXT, true);

    Schema schema(std::move(cols));

    std::vector<Value> values;
    values.emplace_back(1001);
    values.emplace_back("Database Architect");
    values.emplace_back(98.75);
    values.emplace_back(true);
    values.push_back(Value::MakeNull(DataType::TEXT)); // NULL notes

    Tuple tuple(values, schema);

    EXPECT_GT(tuple.GetLength(), 0);

    // Verify individual field extraction
    Value v_id = tuple.GetValue(schema, 0);
    EXPECT_EQ(v_id.GetType(), DataType::INTEGER);
    EXPECT_EQ(v_id.GetInteger(), 1001);

    Value v_name = tuple.GetValue(schema, 1);
    EXPECT_EQ(v_name.GetType(), DataType::TEXT);
    EXPECT_EQ(v_name.GetText(), "Database Architect");

    Value v_score = tuple.GetValue(schema, 2);
    EXPECT_EQ(v_score.GetType(), DataType::DOUBLE);
    EXPECT_DOUBLE_EQ(v_score.GetDouble(), 98.75);

    Value v_active = tuple.GetValue(schema, 3);
    EXPECT_EQ(v_active.GetType(), DataType::BOOLEAN);
    EXPECT_TRUE(v_active.GetBoolean());

    Value v_notes = tuple.GetValue(schema, 4);
    EXPECT_EQ(v_notes.GetType(), DataType::TEXT);
    EXPECT_TRUE(v_notes.IsNull());
}

TEST(TupleTest, StringFormatting) {
    std::vector<Column> cols;
    cols.emplace_back("num", DataType::INTEGER, false);
    cols.emplace_back("msg", DataType::TEXT, false);
    Schema schema(std::move(cols));

    std::vector<Value> values;
    values.emplace_back(42);
    values.emplace_back("hello world");

    Tuple tuple(values, schema);
    EXPECT_EQ(tuple.ToString(schema), "(42, 'hello world')");
}

} // namespace
} // namespace nexus
