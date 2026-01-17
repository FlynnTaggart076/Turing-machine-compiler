#include "Lexer.h"

#include <cctype>

Lexer::Lexer(std::string_view source)
    : source_(source) {}

Token Lexer::next() {
    skipWhitespace();

    if (pos_ >= source_.size()) {
        return {TokenType::Eof, "", line_, column_};
    }

    const int startLine = line_;
    const int startCol = column_;
    const char c = source_[pos_];

    if (c == ';') {
        advance();
        return {TokenType::Semicolon, ";", startLine, startCol};
    }

    if (c == '{') {
        advance();
        return {TokenType::LBrace, "{", startLine, startCol};
    }

    if (c == '}') {
        advance();
        return {TokenType::RBrace, "}", startLine, startCol};
    }

    if (c == '(') {
        advance();
        return {TokenType::LParen, "(", startLine, startCol};
    }

    if (c == ')') {
        advance();
        return {TokenType::RParen, ")", startLine, startCol};
    }

    // == (двойное равно)
    if (c == '=' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
        advance();
        advance();
        return {TokenType::EqEq, "==", startLine, startCol};
    }

    // = (одинарное равно - присваивание)
    if (c == '=') {
        advance();
        return {TokenType::Assign, "=", startLine, startCol};
    }

    if (c == '!' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '=') {
        advance();
        advance();
        return {TokenType::NotEq, "!=", startLine, startCol};
    }

    // < (меньше)
    if (c == '<') {
        advance();
        return {TokenType::Less, "<", startLine, startCol};
    }

    // > (больше)
    if (c == '>') {
        advance();
        return {TokenType::Greater, ">", startLine, startCol};
    }

    // ++ (инкремент)
    if (c == '+' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '+') {
        advance();
        advance();
        return {TokenType::PlusPlus, "++", startLine, startCol};
    }

    // -- (декремент)
    if (c == '-' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '-') {
        advance();
        advance();
        return {TokenType::MinusMinus, "--", startLine, startCol};
    }

    if (c == '"') {
        return readStringLiteral(startLine, startCol);
    }

    // Числовой литерал (включая отрицательные числа)
    if (std::isdigit(static_cast<unsigned char>(c)) || 
        (c == '-' && pos_ + 1 < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_ + 1])))) {
        return readNumber(startLine, startCol);
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return readIdentifier(startLine, startCol);
    }

    std::string val;
    val.push_back(c);
    advance();
    return {TokenType::Unknown, val, startLine, startCol};
}

void Lexer::advance() {
    if (pos_ < source_.size()) {
        if (source_[pos_] == '\n') {
            line_++;
            column_ = 1;
        } else {
            column_++;
        }
        pos_++;
    }
}

void Lexer::skipWhitespace() {
    while (pos_ < source_.size()) {
        const char c = source_[pos_];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            advance();
        }
        else if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '/') {
            while (pos_ < source_.size() && source_[pos_] != '\n') {
                advance();
            }
        }
        else if (c == '/' && pos_ + 1 < source_.size() && source_[pos_ + 1] == '*') {
            advance();
            advance();
            while (pos_ + 1 < source_.size()) {
                if (source_[pos_] == '*' && source_[pos_ + 1] == '/') {
                    advance();
                    advance();
                    break;
                }
                advance();
            }
        }
        else {
            break;
        }
    }
}

Token Lexer::readStringLiteral(int startLine, int startCol) {
    advance();
    std::string value;

    while (pos_ < source_.size() && source_[pos_] != '"') {
        if (source_[pos_] == '\n') {
            return {TokenType::Unknown, value, startLine, startCol};
        }
        value.push_back(source_[pos_]);
        advance();
    }

    if (pos_ >= source_.size()) {
        return {TokenType::Unknown, value, startLine, startCol};
    }

    advance();
    return {TokenType::StringLiteral, value, startLine, startCol};
}

Token Lexer::readIdentifier(int startLine, int startCol) {
    std::string value;

    while (pos_ < source_.size()) {
        const char c = source_[pos_];
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            value.push_back(c);
            advance();
        } else {
            break;
        }
    }

    return {TokenType::Identifier, value, startLine, startCol};
}

Token Lexer::readNumber(int startLine, int startCol) {
    std::string value;

    // Обработка знака минус для отрицательных чисел
    if (source_[pos_] == '-') {
        value.push_back('-');
        advance();
    }

    // Считываем цифры
    while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) {
        value.push_back(source_[pos_]);
        advance();
    }

    return {TokenType::Number, value, startLine, startCol};
}
