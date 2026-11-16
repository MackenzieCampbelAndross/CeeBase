#pragma once

#include <memory>
#include <span>
#include <vector>

#include "ceebase/common/result.hpp"
#include "ceebase/common/status.hpp"
#include "ceebase/parser/ast.hpp"
#include "ceebase/parser/token.hpp"

namespace ceebase {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)), cursor_(0) {}

    Result<std::unique_ptr<Statement>> ParseStatement();
    Result<std::unique_ptr<Expression>> ParseExpression();

private:
    [[nodiscard]] const Token& Peek() const noexcept;
    [[nodiscard]] const Token& Previous() const noexcept;
    [[nodiscard]] bool IsAtEnd() const noexcept;
    const Token& Advance() noexcept;
    bool Check(TokenType type) const noexcept;
    bool Match(TokenType type) noexcept;
    Result<const Token*> Expect(TokenType type, std::string_view err_msg);

    // Statements
    Result<std::unique_ptr<Statement>> ParseCreateTable();
    Result<std::unique_ptr<Statement>> ParseDropTable();
    Result<std::unique_ptr<Statement>> ParseCreateIndex();
    Result<std::unique_ptr<Statement>> ParseInsert();
    Result<std::unique_ptr<Statement>> ParseSelect();
    Result<std::unique_ptr<Statement>> ParseUpdate();
    Result<std::unique_ptr<Statement>> ParseDelete();
    Result<std::unique_ptr<Statement>> ParseExplain();

    // Expressions (Precedence Climbing)
    Result<std::unique_ptr<Expression>> ParseOr();
    Result<std::unique_ptr<Expression>> ParseAnd();
    Result<std::unique_ptr<Expression>> ParseComparison();
    Result<std::unique_ptr<Expression>> ParseAddition();
    Result<std::unique_ptr<Expression>> ParseMultiplication();
    Result<std::unique_ptr<Expression>> ParsePrimary();

    std::vector<Token> tokens_;
    size_t cursor_{0};
};

} // namespace ceebase
