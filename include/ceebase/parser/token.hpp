#pragma once

#include <string>
#include <string_view>

namespace ceebase {

enum class TokenType {
    // End of input / invalid
    END_OF_FILE,
    INVALID,

    // Literals & Identifiers
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,

    // Keywords
    SELECT,
    FROM,
    WHERE,
    INSERT,
    INTO,
    VALUES,
    UPDATE,
    SET,
    DELETE_KW,
    CREATE,
    TABLE,
    DROP,
    INDEX,
    ON,
    ORDER,
    BY,
    ASC,
    DESC,
    LIMIT,
    AND,
    OR,
    NOT,
    AS,
    JOIN,
    INNER,
    GROUP,
    HAVING,
    COUNT,
    SUM,
    AVG,
    MIN,
    MAX,
    EXPLAIN,
    PRIMARY,
    KEY,
    TRUE_KW,
    FALSE_KW,
    NULL_KW,
    IF_KW,
    EXISTS_KW,

    // Data type keywords
    TYPE_INTEGER,
    TYPE_BIGINT,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_BOOLEAN,
    TYPE_TEXT,

    // Operators & Punctuation
    EQUALS,         // =
    NOT_EQUALS,     // != or <>
    LESS,           // <
    LESS_EQUALS,    // <=
    GREATER,        // >
    GREATER_EQUALS, // >=
    PLUS,           // +
    MINUS,          // -
    STAR,           // *
    SLASH,          // /
    DOT,            // .
    COMMA,          // ,
    SEMICOLON,      // ;
    LPAREN,         // (
    RPAREN          // )
};

[[nodiscard]] std::string_view TokenTypeToString(TokenType type) noexcept;

struct Token {
    TokenType type{TokenType::INVALID};
    std::string text{};
    size_t line{1};
    size_t col{1};

    [[nodiscard]] bool Is(TokenType t) const noexcept { return type == t; }
};

} // namespace ceebase
