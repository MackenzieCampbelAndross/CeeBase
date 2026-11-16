#include "ceebase/parser/parser.hpp"

#include <stdexcept>

namespace ceebase {

const Token& Parser::Peek() const noexcept {
    return tokens_[cursor_];
}

const Token& Parser::Previous() const noexcept {
    return tokens_[cursor_ - 1];
}

bool Parser::IsAtEnd() const noexcept {
    return cursor_ >= tokens_.size() || tokens_[cursor_].type == TokenType::END_OF_FILE;
}

const Token& Parser::Advance() noexcept {
    if (!IsAtEnd()) {
        cursor_++;
    }
    return Previous();
}

bool Parser::Check(TokenType type) const noexcept {
    if (IsAtEnd()) return false;
    return Peek().type == type;
}

bool Parser::Match(TokenType type) noexcept {
    if (Check(type)) {
        Advance();
        return true;
    }
    return false;
}

Result<const Token*> Parser::Expect(TokenType type, std::string_view err_msg) {
    if (Check(type)) {
        return &Advance();
    }
    const Token& cur = Peek();
    return Status::InvalidArgument("Syntax error at line " + std::to_string(cur.line) +
                                   ", col " + std::to_string(cur.col) + ": " + std::string(err_msg) +
                                   " (got '" + cur.text + "')");
}

Result<std::unique_ptr<Statement>> Parser::ParseStatement() {
    if (IsAtEnd()) {
        return Status::InvalidArgument("Parser: Empty query statement");
    }

    std::unique_ptr<Statement> stmt;
    if (Match(TokenType::EXPLAIN)) {
        auto child_res = ParseStatement();
        if (!child_res.ok()) return child_res.status();
        return std::unique_ptr<Statement>(std::make_unique<ExplainStatement>(std::move(*child_res)));
    } else if (Match(TokenType::CREATE)) {
        if (Match(TokenType::TABLE)) {
            auto res = ParseCreateTable();
            if (!res.ok()) return res.status();
            stmt = std::move(*res);
        } else if (Match(TokenType::INDEX)) {
            auto res = ParseCreateIndex();
            if (!res.ok()) return res.status();
            stmt = std::move(*res);
        } else {
            return Status::InvalidArgument("Parser: Expected TABLE or INDEX after CREATE");
        }
    } else if (Match(TokenType::DROP)) {
        if (Match(TokenType::TABLE)) {
            auto res = ParseDropTable();
            if (!res.ok()) return res.status();
            stmt = std::move(*res);
        } else {
            return Status::InvalidArgument("Parser: Expected TABLE after DROP");
        }
    } else if (Match(TokenType::INSERT)) {
        auto res = ParseInsert();
        if (!res.ok()) return res.status();
        stmt = std::move(*res);
    } else if (Match(TokenType::SELECT)) {
        auto res = ParseSelect();
        if (!res.ok()) return res.status();
        stmt = std::move(*res);
    } else if (Match(TokenType::UPDATE)) {
        auto res = ParseUpdate();
        if (!res.ok()) return res.status();
        stmt = std::move(*res);
    } else if (Match(TokenType::DELETE_KW)) {
        auto res = ParseDelete();
        if (!res.ok()) return res.status();
        stmt = std::move(*res);
    } else {
        return Status::InvalidArgument("Parser: Unknown SQL statement starting with '" + Peek().text + "'");
    }

    // Optional trailing semicolon
    Match(TokenType::SEMICOLON);
    return stmt;
}

Result<std::unique_ptr<Statement>> Parser::ParseCreateTable() {
    auto name_tok = Expect(TokenType::IDENTIFIER, "Expected table name in CREATE TABLE");
    if (!name_tok.ok()) return name_tok.status();
    std::string table_name = (*name_tok)->text;

    auto lparen = Expect(TokenType::LPAREN, "Expected '(' after table name");
    if (!lparen.ok()) return lparen.status();

    std::vector<ColumnDefinition> columns;
    while (!Check(TokenType::RPAREN) && !IsAtEnd()) {
        auto col_name = Expect(TokenType::IDENTIFIER, "Expected column name");
        if (!col_name.ok()) return col_name.status();

        DataType col_type = DataType::INVALID;
        if (Match(TokenType::TYPE_INTEGER)) col_type = DataType::INTEGER;
        else if (Match(TokenType::TYPE_BIGINT)) col_type = DataType::BIGINT;
        else if (Match(TokenType::TYPE_FLOAT)) col_type = DataType::FLOAT;
        else if (Match(TokenType::TYPE_DOUBLE)) col_type = DataType::DOUBLE;
        else if (Match(TokenType::TYPE_BOOLEAN)) col_type = DataType::BOOLEAN;
        else if (Match(TokenType::TYPE_TEXT)) col_type = DataType::TEXT;
        else {
            return Status::InvalidArgument("Expected valid data type for column '" + (*col_name)->text + "'");
        }

        bool is_pk = false;
        if (Match(TokenType::PRIMARY)) {
            auto key_tok = Expect(TokenType::KEY, "Expected KEY after PRIMARY");
            if (!key_tok.ok()) return key_tok.status();
            is_pk = true;
        }

        columns.push_back(ColumnDefinition{(*col_name)->text, col_type, !is_pk, is_pk});

        if (!Match(TokenType::COMMA)) {
            break;
        }
    }

    auto rparen = Expect(TokenType::RPAREN, "Expected ')' closing column definitions");
    if (!rparen.ok()) return rparen.status();

    return std::unique_ptr<Statement>(std::make_unique<CreateTableStatement>(table_name, std::move(columns)));
}

Result<std::unique_ptr<Statement>> Parser::ParseDropTable() {
    auto name_tok = Expect(TokenType::IDENTIFIER, "Expected table name in DROP TABLE");
    if (!name_tok.ok()) return name_tok.status();
    return std::unique_ptr<Statement>(std::make_unique<DropTableStatement>((*name_tok)->text));
}

Result<std::unique_ptr<Statement>> Parser::ParseCreateIndex() {
    auto idx_tok = Expect(TokenType::IDENTIFIER, "Expected index name in CREATE INDEX");
    if (!idx_tok.ok()) return idx_tok.status();

    auto on_tok = Expect(TokenType::ON, "Expected ON after index name");
    if (!on_tok.ok()) return on_tok.status();

    auto tbl_tok = Expect(TokenType::IDENTIFIER, "Expected table name in CREATE INDEX");
    if (!tbl_tok.ok()) return tbl_tok.status();

    auto lparen = Expect(TokenType::LPAREN, "Expected '(' before column name");
    if (!lparen.ok()) return lparen.status();

    auto col_tok = Expect(TokenType::IDENTIFIER, "Expected column name for index");
    if (!col_tok.ok()) return col_tok.status();

    auto rparen = Expect(TokenType::RPAREN, "Expected ')' after column name");
    if (!rparen.ok()) return rparen.status();

    return std::unique_ptr<Statement>(std::make_unique<CreateIndexStatement>((*idx_tok)->text, (*tbl_tok)->text, (*col_tok)->text));
}

Result<std::unique_ptr<Statement>> Parser::ParseInsert() {
    auto into = Expect(TokenType::INTO, "Expected INTO after INSERT");
    if (!into.ok()) return into.status();

    auto tbl = Expect(TokenType::IDENTIFIER, "Expected table name in INSERT INTO");
    if (!tbl.ok()) return tbl.status();
    std::string table_name = (*tbl)->text;

    std::vector<std::string> columns;
    if (Match(TokenType::LPAREN)) {
        while (!Check(TokenType::RPAREN) && !IsAtEnd()) {
            auto col = Expect(TokenType::IDENTIFIER, "Expected column name in column list");
            if (!col.ok()) return col.status();
            columns.push_back((*col)->text);
            if (!Match(TokenType::COMMA)) break;
        }
        auto rparen = Expect(TokenType::RPAREN, "Expected ')' after column list");
        if (!rparen.ok()) return rparen.status();
    }

    auto values_kw = Expect(TokenType::VALUES, "Expected VALUES in INSERT");
    if (!values_kw.ok()) return values_kw.status();

    std::vector<std::vector<std::unique_ptr<Expression>>> values_list;
    while (!IsAtEnd()) {
        auto lparen = Expect(TokenType::LPAREN, "Expected '(' beginning VALUES row tuple");
        if (!lparen.ok()) return lparen.status();

        std::vector<std::unique_ptr<Expression>> row;
        while (!Check(TokenType::RPAREN) && !IsAtEnd()) {
            auto expr_res = ParseExpression();
            if (!expr_res.ok()) return expr_res.status();
            row.push_back(std::move(*expr_res));
            if (!Match(TokenType::COMMA)) break;
        }

        auto rparen = Expect(TokenType::RPAREN, "Expected ')' ending VALUES row tuple");
        if (!rparen.ok()) return rparen.status();

        values_list.push_back(std::move(row));
        if (!Match(TokenType::COMMA)) {
            break;
        }
    }

    return std::unique_ptr<Statement>(std::make_unique<InsertStatement>(table_name, std::move(columns), std::move(values_list)));
}

Result<std::unique_ptr<Statement>> Parser::ParseSelect() {
    std::vector<std::unique_ptr<Expression>> select_list;
    while (!Check(TokenType::FROM) && !IsAtEnd()) {
        if (Match(TokenType::STAR)) {
            select_list.push_back(std::make_unique<ColumnRefExpr>("*"));
        } else {
            auto expr_res = ParseExpression();
            if (!expr_res.ok()) return expr_res.status();
            select_list.push_back(std::move(*expr_res));
        }
        if (!Match(TokenType::COMMA)) {
            break;
        }
    }

    auto from_tok = Expect(TokenType::FROM, "Expected FROM in SELECT query");
    if (!from_tok.ok()) return from_tok.status();

    auto tbl_tok = Expect(TokenType::IDENTIFIER, "Expected table name in FROM clause");
    if (!tbl_tok.ok()) return tbl_tok.status();
    std::string from_table = (*tbl_tok)->text;

    std::string join_table = "";
    std::unique_ptr<Expression> join_condition = nullptr;

    if (Match(TokenType::INNER) || Match(TokenType::JOIN)) {
        if (Previous().type == TokenType::INNER) {
            auto j_tok = Expect(TokenType::JOIN, "Expected JOIN after INNER");
            if (!j_tok.ok()) return j_tok.status();
        }
        auto jtbl = Expect(TokenType::IDENTIFIER, "Expected table name for JOIN");
        if (!jtbl.ok()) return jtbl.status();
        join_table = (*jtbl)->text;

        auto on_tok = Expect(TokenType::ON, "Expected ON after JOIN table");
        if (!on_tok.ok()) return on_tok.status();

        auto cond_res = ParseExpression();
        if (!cond_res.ok()) return cond_res.status();
        join_condition = std::move(*cond_res);
    }

    std::unique_ptr<Expression> where_clause = nullptr;
    if (Match(TokenType::WHERE)) {
        auto where_res = ParseExpression();
        if (!where_res.ok()) return where_res.status();
        where_clause = std::move(*where_res);
    }

    std::vector<std::string> group_by;
    if (Match(TokenType::GROUP)) {
        auto by_tok = Expect(TokenType::BY, "Expected BY after GROUP");
        if (!by_tok.ok()) return by_tok.status();

        while (!IsAtEnd()) {
            auto col = Expect(TokenType::IDENTIFIER, "Expected column name in GROUP BY");
            if (!col.ok()) return col.status();
            group_by.push_back((*col)->text);
            if (!Match(TokenType::COMMA)) break;
        }
    }

    std::unique_ptr<Expression> having_clause = nullptr;
    if (Match(TokenType::HAVING)) {
        auto hav_res = ParseExpression();
        if (!hav_res.ok()) return hav_res.status();
        having_clause = std::move(*hav_res);
    }

    std::string order_by_column = "";
    bool is_desc = false;
    if (Match(TokenType::ORDER)) {
        auto by_tok = Expect(TokenType::BY, "Expected BY after ORDER");
        if (!by_tok.ok()) return by_tok.status();

        auto col = Expect(TokenType::IDENTIFIER, "Expected column name in ORDER BY");
        if (!col.ok()) return col.status();
        order_by_column = (*col)->text;

        if (Match(TokenType::DESC)) {
            is_desc = true;
        } else {
            Match(TokenType::ASC);
        }
    }

    int limit = -1;
    if (Match(TokenType::LIMIT)) {
        auto limit_tok = Expect(TokenType::INT_LITERAL, "Expected integer in LIMIT clause");
        if (!limit_tok.ok()) return limit_tok.status();
        limit = std::stoi((*limit_tok)->text);
    }

    return std::unique_ptr<Statement>(std::make_unique<SelectStatement>(std::move(select_list), from_table, join_table,
                                                                         std::move(join_condition), std::move(where_clause),
                                                                         std::move(group_by), std::move(having_clause),
                                                                         order_by_column, is_desc, limit));
}

Result<std::unique_ptr<Statement>> Parser::ParseUpdate() {
    auto tbl = Expect(TokenType::IDENTIFIER, "Expected table name in UPDATE");
    if (!tbl.ok()) return tbl.status();
    std::string table_name = (*tbl)->text;

    auto set_tok = Expect(TokenType::SET, "Expected SET in UPDATE");
    if (!set_tok.ok()) return set_tok.status();

    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> assignments;
    while (!IsAtEnd()) {
        auto col = Expect(TokenType::IDENTIFIER, "Expected column name in UPDATE assignment");
        if (!col.ok()) return col.status();

        auto eq = Expect(TokenType::EQUALS, "Expected '=' in UPDATE assignment");
        if (!eq.ok()) return eq.status();

        auto expr_res = ParseExpression();
        if (!expr_res.ok()) return expr_res.status();

        assignments.emplace_back((*col)->text, std::move(*expr_res));
        if (!Match(TokenType::COMMA)) break;
    }

    std::unique_ptr<Expression> where_clause = nullptr;
    if (Match(TokenType::WHERE)) {
        auto where_res = ParseExpression();
        if (!where_res.ok()) return where_res.status();
        where_clause = std::move(*where_res);
    }

    return std::unique_ptr<Statement>(std::make_unique<UpdateStatement>(table_name, std::move(assignments), std::move(where_clause)));
}

Result<std::unique_ptr<Statement>> Parser::ParseDelete() {
    auto from = Expect(TokenType::FROM, "Expected FROM after DELETE");
    if (!from.ok()) return from.status();

    auto tbl = Expect(TokenType::IDENTIFIER, "Expected table name in DELETE FROM");
    if (!tbl.ok()) return tbl.status();
    std::string table_name = (*tbl)->text;

    std::unique_ptr<Expression> where_clause = nullptr;
    if (Match(TokenType::WHERE)) {
        auto where_res = ParseExpression();
        if (!where_res.ok()) return where_res.status();
        where_clause = std::move(*where_res);
    }

    return std::unique_ptr<Statement>(std::make_unique<DeleteStatement>(table_name, std::move(where_clause)));
}

Result<std::unique_ptr<Expression>> Parser::ParseExpression() {
    return ParseOr();
}

Result<std::unique_ptr<Expression>> Parser::ParseOr() {
    auto left_res = ParseAnd();
    if (!left_res.ok()) return left_res;
    auto expr = std::move(*left_res);

    while (Match(TokenType::OR)) {
        auto right_res = ParseAnd();
        if (!right_res.ok()) return right_res;
        expr = std::make_unique<BinaryOpExpr>(std::move(expr), BinaryOp::OR, std::move(*right_res));
    }
    return expr;
}

Result<std::unique_ptr<Expression>> Parser::ParseAnd() {
    auto left_res = ParseComparison();
    if (!left_res.ok()) return left_res;
    auto expr = std::move(*left_res);

    while (Match(TokenType::AND)) {
        auto right_res = ParseComparison();
        if (!right_res.ok()) return right_res;
        expr = std::make_unique<BinaryOpExpr>(std::move(expr), BinaryOp::AND, std::move(*right_res));
    }
    return expr;
}

Result<std::unique_ptr<Expression>> Parser::ParseComparison() {
    auto left_res = ParseAddition();
    if (!left_res.ok()) return left_res;
    auto expr = std::move(*left_res);

    while (true) {
        BinaryOp op;
        if (Match(TokenType::EQUALS)) op = BinaryOp::EQUALS;
        else if (Match(TokenType::NOT_EQUALS)) op = BinaryOp::NOT_EQUALS;
        else if (Match(TokenType::LESS)) op = BinaryOp::LESS;
        else if (Match(TokenType::LESS_EQUALS)) op = BinaryOp::LESS_EQUALS;
        else if (Match(TokenType::GREATER)) op = BinaryOp::GREATER;
        else if (Match(TokenType::GREATER_EQUALS)) op = BinaryOp::GREATER_EQUALS;
        else break;

        auto right_res = ParseAddition();
        if (!right_res.ok()) return right_res;
        expr = std::make_unique<BinaryOpExpr>(std::move(expr), op, std::move(*right_res));
    }
    return expr;
}

Result<std::unique_ptr<Expression>> Parser::ParseAddition() {
    auto left_res = ParseMultiplication();
    if (!left_res.ok()) return left_res;
    auto expr = std::move(*left_res);

    while (true) {
        BinaryOp op;
        if (Match(TokenType::PLUS)) op = BinaryOp::ADD;
        else if (Match(TokenType::MINUS)) op = BinaryOp::SUBTRACT;
        else break;

        auto right_res = ParseMultiplication();
        if (!right_res.ok()) return right_res;
        expr = std::make_unique<BinaryOpExpr>(std::move(expr), op, std::move(*right_res));
    }
    return expr;
}

Result<std::unique_ptr<Expression>> Parser::ParseMultiplication() {
    auto left_res = ParsePrimary();
    if (!left_res.ok()) return left_res;
    auto expr = std::move(*left_res);

    while (true) {
        BinaryOp op;
        if (Match(TokenType::STAR)) op = BinaryOp::MULTIPLY;
        else if (Match(TokenType::SLASH)) op = BinaryOp::DIVIDE;
        else break;

        auto right_res = ParsePrimary();
        if (!right_res.ok()) return right_res;
        expr = std::make_unique<BinaryOpExpr>(std::move(expr), op, std::move(*right_res));
    }
    return expr;
}

Result<std::unique_ptr<Expression>> Parser::ParsePrimary() {
    if (Match(TokenType::INT_LITERAL)) {
        int64_t val = std::stoll(Previous().text);
        if (val >= std::numeric_limits<int32_t>::min() && val <= std::numeric_limits<int32_t>::max()) {
            return std::unique_ptr<Expression>(std::make_unique<LiteralExpr>(Value(static_cast<int32_t>(val))));
        }
        return std::unique_ptr<Expression>(std::make_unique<LiteralExpr>(Value(val)));
    }

    if (Match(TokenType::FLOAT_LITERAL)) {
        double val = std::stod(Previous().text);
        return std::unique_ptr<Expression>(std::make_unique<LiteralExpr>(Value(val)));
    }

    if (Match(TokenType::STRING_LITERAL)) {
        return std::unique_ptr<Expression>(std::make_unique<LiteralExpr>(Value(Previous().text)));
    }

    if (Match(TokenType::TRUE_KW)) {
        return std::unique_ptr<Expression>(std::make_unique<LiteralExpr>(Value(true)));
    }

    if (Match(TokenType::FALSE_KW)) {
        return std::unique_ptr<Expression>(std::make_unique<LiteralExpr>(Value(false)));
    }

    if (Match(TokenType::NULL_KW)) {
        return std::unique_ptr<Expression>(std::make_unique<LiteralExpr>(Value()));
    }

    // Aggregate Functions: COUNT, SUM, AVG, MIN, MAX
    if (Check(TokenType::COUNT) || Check(TokenType::SUM) || Check(TokenType::AVG) ||
        Check(TokenType::MIN) || Check(TokenType::MAX)) {
        AggregateType agg_type;
        if (Match(TokenType::COUNT)) agg_type = AggregateType::COUNT;
        else if (Match(TokenType::SUM)) agg_type = AggregateType::SUM;
        else if (Match(TokenType::AVG)) agg_type = AggregateType::AVG;
        else if (Match(TokenType::MIN)) agg_type = AggregateType::MIN;
        else { Match(TokenType::MAX); agg_type = AggregateType::MAX; }

        auto lparen = Expect(TokenType::LPAREN, "Expected '(' after aggregate function");
        if (!lparen.ok()) return lparen.status();

        bool is_star = false;
        std::unique_ptr<Expression> arg = nullptr;
        if (Match(TokenType::STAR)) {
            is_star = true;
        } else {
            auto arg_res = ParseExpression();
            if (!arg_res.ok()) return arg_res.status();
            arg = std::move(*arg_res);
        }

        auto rparen = Expect(TokenType::RPAREN, "Expected ')' closing aggregate function");
        if (!rparen.ok()) return rparen.status();

        return std::unique_ptr<Expression>(std::make_unique<AggregateExpr>(agg_type, std::move(arg), is_star));
    }

    // Column Reference: identifier or table.identifier
    if (Match(TokenType::IDENTIFIER)) {
        std::string name1 = Previous().text;
        if (Match(TokenType::STAR)) {
            return std::unique_ptr<Expression>(std::make_unique<ColumnRefExpr>("*", name1));
        }
        // Check for table.column
        if (Match(TokenType::DOT)) {
            auto name2 = Expect(TokenType::IDENTIFIER, "Expected column name after '.'");
            if (!name2.ok()) return name2.status();
            return std::unique_ptr<Expression>(std::make_unique<ColumnRefExpr>((*name2)->text, name1));
        }
        return std::unique_ptr<Expression>(std::make_unique<ColumnRefExpr>(name1));
    }

    if (Match(TokenType::LPAREN)) {
        auto expr_res = ParseExpression();
        if (!expr_res.ok()) return expr_res.status();
        auto rparen = Expect(TokenType::RPAREN, "Expected ')' after parenthesized expression");
        if (!rparen.ok()) return rparen.status();
        return expr_res;
    }

    const Token& cur = Peek();
    return Status::InvalidArgument("Syntax error: unexpected token '" + cur.text + "' in expression");
}

} // namespace ceebase
