#include "ceebase/parser/lexer.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace ceebase {

std::string_view TokenTypeToString(TokenType type) noexcept {
    switch (type) {
        case TokenType::END_OF_FILE: return "EOF";
        case TokenType::INVALID: return "INVALID";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::INT_LITERAL: return "INT_LITERAL";
        case TokenType::FLOAT_LITERAL: return "FLOAT_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::SELECT: return "SELECT";
        case TokenType::FROM: return "FROM";
        case TokenType::WHERE: return "WHERE";
        case TokenType::INSERT: return "INSERT";
        case TokenType::INTO: return "INTO";
        case TokenType::VALUES: return "VALUES";
        case TokenType::UPDATE: return "UPDATE";
        case TokenType::SET: return "SET";
        case TokenType::DELETE_KW: return "DELETE";
        case TokenType::CREATE: return "CREATE";
        case TokenType::TABLE: return "TABLE";
        case TokenType::DROP: return "DROP";
        case TokenType::INDEX: return "INDEX";
        case TokenType::ON: return "ON";
        case TokenType::ORDER: return "ORDER";
        case TokenType::BY: return "BY";
        case TokenType::ASC: return "ASC";
        case TokenType::DESC: return "DESC";
        case TokenType::LIMIT: return "LIMIT";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::NOT: return "NOT";
        case TokenType::AS: return "AS";
        case TokenType::JOIN: return "JOIN";
        case TokenType::INNER: return "INNER";
        case TokenType::GROUP: return "GROUP";
        case TokenType::HAVING: return "HAVING";
        case TokenType::COUNT: return "COUNT";
        case TokenType::SUM: return "SUM";
        case TokenType::AVG: return "AVG";
        case TokenType::MIN: return "MIN";
        case TokenType::MAX: return "MAX";
        case TokenType::EXPLAIN: return "EXPLAIN";
        case TokenType::PRIMARY: return "PRIMARY";
        case TokenType::KEY: return "KEY";
        case TokenType::TRUE_KW: return "TRUE";
        case TokenType::FALSE_KW: return "FALSE";
        case TokenType::NULL_KW: return "NULL";
        case TokenType::TYPE_INTEGER: return "INTEGER";
        case TokenType::TYPE_BIGINT: return "BIGINT";
        case TokenType::TYPE_FLOAT: return "FLOAT";
        case TokenType::TYPE_DOUBLE: return "DOUBLE";
        case TokenType::TYPE_BOOLEAN: return "BOOLEAN";
        case TokenType::TYPE_TEXT: return "TEXT";
        case TokenType::EQUALS: return "=";
        case TokenType::NOT_EQUALS: return "!=";
        case TokenType::LESS: return "<";
        case TokenType::LESS_EQUALS: return "<=";
        case TokenType::GREATER: return ">";
        case TokenType::GREATER_EQUALS: return ">=";
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::STAR: return "*";
        case TokenType::SLASH: return "/";
        case TokenType::DOT: return ".";
        case TokenType::COMMA: return ",";
        case TokenType::SEMICOLON: return ";";
        case TokenType::LPAREN: return "(";
        case TokenType::RPAREN: return ")";
        default: return "UNKNOWN";
    }
}

namespace {

const std::unordered_map<std::string, TokenType> kKeywords = {
    {"SELECT", TokenType::SELECT},
    {"FROM", TokenType::FROM},
    {"WHERE", TokenType::WHERE},
    {"INSERT", TokenType::INSERT},
    {"INTO", TokenType::INTO},
    {"VALUES", TokenType::VALUES},
    {"UPDATE", TokenType::UPDATE},
    {"SET", TokenType::SET},
    {"DELETE", TokenType::DELETE_KW},
    {"CREATE", TokenType::CREATE},
    {"TABLE", TokenType::TABLE},
    {"DROP", TokenType::DROP},
    {"INDEX", TokenType::INDEX},
    {"ON", TokenType::ON},
    {"ORDER", TokenType::ORDER},
    {"BY", TokenType::BY},
    {"ASC", TokenType::ASC},
    {"DESC", TokenType::DESC},
    {"LIMIT", TokenType::LIMIT},
    {"AND", TokenType::AND},
    {"OR", TokenType::OR},
    {"NOT", TokenType::NOT},
    {"AS", TokenType::AS},
    {"JOIN", TokenType::JOIN},
    {"INNER", TokenType::INNER},
    {"GROUP", TokenType::GROUP},
    {"HAVING", TokenType::HAVING},
    {"COUNT", TokenType::COUNT},
    {"SUM", TokenType::SUM},
    {"AVG", TokenType::AVG},
    {"MIN", TokenType::MIN},
    {"MAX", TokenType::MAX},
    {"EXPLAIN", TokenType::EXPLAIN},
    {"PRIMARY", TokenType::PRIMARY},
    {"KEY", TokenType::KEY},
    {"TRUE", TokenType::TRUE_KW},
    {"FALSE", TokenType::FALSE_KW},
    {"NULL", TokenType::NULL_KW},
    {"INTEGER", TokenType::TYPE_INTEGER},
    {"INT", TokenType::TYPE_INTEGER},
    {"BIGINT", TokenType::TYPE_BIGINT},
    {"FLOAT", TokenType::TYPE_FLOAT},
    {"DOUBLE", TokenType::TYPE_DOUBLE},
    {"BOOLEAN", TokenType::TYPE_BOOLEAN},
    {"BOOL", TokenType::TYPE_BOOLEAN},
    {"TEXT", TokenType::TYPE_TEXT},
    {"VARCHAR", TokenType::TYPE_TEXT}
};

} // namespace

char Lexer::Peek() const noexcept {
    if (IsAtEnd()) return '\0';
    return source_[cursor_];
}

char Lexer::PeekNext() const noexcept {
    if (cursor_ + 1 >= source_.size()) return '\0';
    return source_[cursor_ + 1];
}

char Lexer::Advance() noexcept {
    char c = source_[cursor_++];
    col_++;
    return c;
}

bool Lexer::Match(char expected) noexcept {
    if (IsAtEnd() || source_[cursor_] != expected) {
        return false;
    }
    cursor_++;
    col_++;
    return true;
}

void Lexer::SkipWhitespaceAndComments() {
    while (!IsAtEnd()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            Advance();
        } else if (c == '\n') {
            cursor_++;
            line_++;
            col_ = 1;
        } else if (c == '-' && PeekNext() == '-') {
            // SQL comment until end of line
            while (!IsAtEnd() && Peek() != '\n') {
                Advance();
            }
        } else {
            break;
        }
    }
}

Result<std::vector<Token>> Lexer::Tokenize() {
    std::vector<Token> tokens;
    while (true) {
        SkipWhitespaceAndComments();
        if (IsAtEnd()) {
            tokens.push_back(Token{TokenType::END_OF_FILE, "", line_, col_});
            break;
        }
        Token tok = ScanToken();
        if (tok.type == TokenType::INVALID) {
            return Status::InvalidArgument("Syntax error at line " + std::to_string(tok.line) +
                                           ", col " + std::to_string(tok.col) + ": unexpected character '" + tok.text + "'");
        }
        tokens.push_back(std::move(tok));
    }
    return tokens;
}

Token Lexer::ScanToken() {
    size_t start_col = col_;
    size_t start_line = line_;
    char c = Advance();

    if (std::isalpha(c) || c == '_') {
        cursor_--;
        col_--;
        return ScanIdentifierOrKeyword();
    }

    if (std::isdigit(c)) {
        cursor_--;
        col_--;
        return ScanNumber();
    }

    if (c == '\'') {
        cursor_--;
        col_--;
        return ScanString();
    }

    switch (c) {
        case '=': return Token{TokenType::EQUALS, "=", start_line, start_col};
        case '!':
            if (Match('=')) return Token{TokenType::NOT_EQUALS, "!=", start_line, start_col};
            return Token{TokenType::INVALID, "!", start_line, start_col};
        case '<':
            if (Match('=')) return Token{TokenType::LESS_EQUALS, "<=", start_line, start_col};
            if (Match('>')) return Token{TokenType::NOT_EQUALS, "<>", start_line, start_col};
            return Token{TokenType::LESS, "<", start_line, start_col};
        case '>':
            if (Match('=')) return Token{TokenType::GREATER_EQUALS, ">=", start_line, start_col};
            return Token{TokenType::GREATER, ">", start_line, start_col};
        case '+': return Token{TokenType::PLUS, "+", start_line, start_col};
        case '-': return Token{TokenType::MINUS, "-", start_line, start_col};
        case '*': return Token{TokenType::STAR, "*", start_line, start_col};
        case '/': return Token{TokenType::SLASH, "/", start_line, start_col};
        case '.': return Token{TokenType::DOT, ".", start_line, start_col};
        case ',': return Token{TokenType::COMMA, ",", start_line, start_col};
        case ';': return Token{TokenType::SEMICOLON, ";", start_line, start_col};
        case '(': return Token{TokenType::LPAREN, "(", start_line, start_col};
        case ')': return Token{TokenType::RPAREN, ")", start_line, start_col};
        default:
            return Token{TokenType::INVALID, std::string(1, c), start_line, start_col};
    }
}

Token Lexer::ScanIdentifierOrKeyword() {
    size_t start_col = col_;
    size_t start_line = line_;
    size_t start_cursor = cursor_;

    while (!IsAtEnd() && (std::isalnum(Peek()) || Peek() == '_')) {
        Advance();
    }

    std::string text(source_.substr(start_cursor, cursor_ - start_cursor));
    std::string upper_text = text;
    for (char& ch : upper_text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }

    auto it = kKeywords.find(upper_text);
    if (it != kKeywords.end()) {
        return Token{it->second, text, start_line, start_col};
    }

    return Token{TokenType::IDENTIFIER, text, start_line, start_col};
}

Token Lexer::ScanNumber() {
    size_t start_col = col_;
    size_t start_line = line_;
    size_t start_cursor = cursor_;

    while (!IsAtEnd() && std::isdigit(Peek())) {
        Advance();
    }

    bool is_float = false;
    if (Peek() == '.' && std::isdigit(PeekNext())) {
        is_float = true;
        Advance(); // Consume '.'
        while (!IsAtEnd() && std::isdigit(Peek())) {
            Advance();
        }
    }

    std::string text(source_.substr(start_cursor, cursor_ - start_cursor));
    return Token{is_float ? TokenType::FLOAT_LITERAL : TokenType::INT_LITERAL, text, start_line, start_col};
}

Token Lexer::ScanString() {
    size_t start_col = col_;
    size_t start_line = line_;
    Advance(); // consume opening quote '\''

    std::string str;
    while (!IsAtEnd()) {
        if (Peek() == '\'') {
            Advance();
            if (Peek() == '\'') { // Escaped single quote
                str.push_back('\'');
                Advance();
            } else {
                break; // End of string literal
            }
        } else {
            str.push_back(Advance());
        }
    }

    return Token{TokenType::STRING_LITERAL, str, start_line, start_col};
}

} // namespace ceebase
