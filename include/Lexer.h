#pragma once

#include <cstddef>
#include <string>
#include <string_view>

/** @brief Типы токенов исходного кода */
enum class TokenType {
    Eof,            // Конец файла
    Identifier,     // Идентификатор
    StringLiteral,  // Строковый литерал
    Number,         // Числовой литерал
    Semicolon,      // ;
    LBrace,         // {
    RBrace,         // }
    LParen,         // (
    RParen,         // )
    EqEq,           // ==
    NotEq,          // !=
    Assign,         // =
    Less,           // <
    Greater,        // >
    PlusPlus,       // ++
    MinusMinus,     // --
    Unknown         // Неизвестный токен
};

/** @brief Представление лексемы */
struct Token {
    TokenType type{TokenType::Eof};
    std::string value;
    int line{1};
    int column{1};
};

/** @brief Лексический анализатор */
class Lexer {
public:
    explicit Lexer(std::string_view source);

    Token next();

    int line() const { return line_; }
    int column() const { return column_; }

private:
    void advance();
    void skipWhitespace();
    Token readStringLiteral(int startLine, int startCol);
    Token readIdentifier(int startLine, int startCol);
    Token readNumber(int startLine, int startCol);

    std::string_view source_;
    std::size_t pos_{0};
    int line_{1};
    int column_{1};
};
