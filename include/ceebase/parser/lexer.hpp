#pragma once

#include <string_view>
#include <vector>

#include "ceebase/common/result.hpp"
#include "ceebase/common/status.hpp"
#include "ceebase/parser/token.hpp"

namespace ceebase {

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    Result<std::vector<Token>> Tokenize();

private:
    [[nodiscard]] bool IsAtEnd() const noexcept { return cursor_ >= source_.size(); }
    char Peek() const noexcept;
    char PeekNext() const noexcept;
    char Advance() noexcept;
    bool Match(char expected) noexcept;
    void SkipWhitespaceAndComments();

    Token ScanToken();
    Token ScanIdentifierOrKeyword();
    Token ScanNumber();
    Token ScanString();

    std::string_view source_;
    size_t cursor_{0};
    size_t line_{1};
    size_t col_{1};
};

} // namespace ceebase
