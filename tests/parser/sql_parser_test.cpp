#include <gtest/gtest.h>

#include "nexus/parser/ast.hpp"
#include "nexus/parser/lexer.hpp"
#include "nexus/parser/parser.hpp"

namespace nexus {
namespace {

TEST(SqlParserTest, LexerTokenization) {
    std::string sql = "SELECT id, name, balance FROM accounts WHERE balance >= 100.50 AND is_active = true;";
    Lexer lexer(sql);
    auto tokens_res = lexer.Tokenize();
    ASSERT_TRUE(tokens_res.ok());

    const auto& tokens = *tokens_res;
    EXPECT_EQ(tokens[0].type, TokenType::SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].text, "id");
    EXPECT_EQ(tokens[2].type, TokenType::COMMA);
    EXPECT_EQ(tokens[3].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[3].text, "name");
}

TEST(SqlParserTest, ParseCreateTableAndDropTable) {
    std::string sql = "CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT, balance DOUBLE);";
    Lexer lexer(sql);
    auto tokens_res = lexer.Tokenize();
    ASSERT_TRUE(tokens_res.ok());

    Parser parser(std::move(*tokens_res));
    auto stmt_res = parser.ParseStatement();
    ASSERT_TRUE(stmt_res.ok());

    EXPECT_EQ((*stmt_res)->GetType(), StatementType::CREATE_TABLE);
    auto* create_stmt = static_cast<CreateTableStatement*>(stmt_res->get());
    EXPECT_EQ(create_stmt->GetTableName(), "users");
    EXPECT_EQ(create_stmt->GetColumns().size(), 3);
    EXPECT_TRUE(create_stmt->GetColumns()[0].is_primary_key);

    std::string drop_sql = "DROP TABLE users;";
    Lexer drop_lexer(drop_sql);
    Parser drop_parser(std::move(*drop_lexer.Tokenize()));
    auto drop_res = drop_parser.ParseStatement();
    ASSERT_TRUE(drop_res.ok());
    EXPECT_EQ((*drop_res)->GetType(), StatementType::DROP_TABLE);
}

TEST(SqlParserTest, ParseComplexSelectAndExplain) {
    std::string sql = "EXPLAIN SELECT id, name, COUNT(*) FROM employees "
                      "INNER JOIN departments ON employees.dept_id = departments.id "
                      "WHERE salary > 50000 GROUP BY dept_id HAVING COUNT(*) > 5 "
                      "ORDER BY salary DESC LIMIT 10;";
    Lexer lexer(sql);
    auto tokens_res = lexer.Tokenize();
    ASSERT_TRUE(tokens_res.ok());

    Parser parser(std::move(*tokens_res));
    auto stmt_res = parser.ParseStatement();
    ASSERT_TRUE(stmt_res.ok());

    EXPECT_EQ((*stmt_res)->GetType(), StatementType::EXPLAIN);
    auto* explain = static_cast<ExplainStatement*>(stmt_res->get());
    ASSERT_NE(explain->GetStatement(), nullptr);
    EXPECT_EQ(explain->GetStatement()->GetType(), StatementType::SELECT);

    auto* sel = static_cast<const SelectStatement*>(explain->GetStatement());
    EXPECT_EQ(sel->GetFromTable(), "employees");
    EXPECT_EQ(sel->GetJoinTable(), "departments");
    EXPECT_NE(sel->GetJoinCondition(), nullptr);
    EXPECT_NE(sel->GetWhereClause(), nullptr);
    EXPECT_EQ(sel->GetGroupBy().size(), 1);
    EXPECT_NE(sel->GetHavingClause(), nullptr);
    EXPECT_EQ(sel->GetOrderByColumn(), "salary");
    EXPECT_TRUE(sel->IsDesc());
    EXPECT_EQ(sel->GetLimit(), 10);
}

TEST(SqlParserTest, ParseInvalidSyntaxErrorHandling) {
    std::string bad_sql = "SELECT FROM WHERE;";
    Lexer lexer(bad_sql);
    auto tokens = lexer.Tokenize();
    ASSERT_TRUE(tokens.ok());

    Parser parser(std::move(*tokens));
    auto res = parser.ParseStatement();
    EXPECT_FALSE(res.ok());
}

} // namespace
} // namespace nexus
