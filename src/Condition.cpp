#include "Condition.h"

bool evaluateCondition(const ConditionPtr& cond, const Symbol& currentSymbol) {
    switch (cond->type) {
    case ConditionType::ReadEq:
        return currentSymbol == cond->symbol;
    case ConditionType::ReadNeq:
        return currentSymbol != cond->symbol;
    case ConditionType::And:
        return evaluateCondition(cond->left, currentSymbol) && evaluateCondition(cond->right, currentSymbol);
    case ConditionType::Or:
        return evaluateCondition(cond->left, currentSymbol) || evaluateCondition(cond->right, currentSymbol);
    case ConditionType::Xor: {
        bool l = evaluateCondition(cond->left, currentSymbol);
        bool r = evaluateCondition(cond->right, currentSymbol);
        return (l && !r) || (!l && r);
    }
    case ConditionType::Not:
        return !evaluateCondition(cond->operand, currentSymbol);
    }
    return false;
}

ConditionParser::ConditionParser(
    Lexer& lexer,
    Token& currentToken,
    const std::unordered_set<Symbol>& alphabetSet,
    const Symbol& blankSymbol,
    std::vector<Diagnostic>& diagnostics,
    bool& ok)
    : lexer_(lexer)
    , token_(currentToken)
    , alphabetSet_(alphabetSet)
    , blankSymbol_(blankSymbol)
    , diagnostics_(diagnostics)
    , ok_(ok) {}

ConditionPtr ConditionParser::parse() {
    return parseOr();
}

void ConditionParser::error(int line, int col, const std::string& msg) {
    diagnostics_.push_back({DiagnosticLevel::Error, line, col, msg});
    ok_ = false;
}

ConditionPtr ConditionParser::parseOr() {
    auto left = parseXor();
    if (!left) return nullptr;

    while (ok_ && token_.type == TokenType::Identifier && token_.value == "or") {
        token_ = lexer_.next();
        auto right = parseXor();
        if (!right) return nullptr;
        left = Condition::binaryOp(ConditionType::Or, left, right);
    }
    return left;
}

ConditionPtr ConditionParser::parseXor() {
    auto left = parseAnd();
    if (!left) return nullptr;

    while (ok_ && token_.type == TokenType::Identifier && token_.value == "xor") {
        token_ = lexer_.next();
        auto right = parseAnd();
        if (!right) return nullptr;
        left = Condition::binaryOp(ConditionType::Xor, left, right);
    }
    return left;
}

ConditionPtr ConditionParser::parseAnd() {
    auto left = parseNot();
    if (!left) return nullptr;

    while (ok_ && token_.type == TokenType::Identifier && token_.value == "and") {
        token_ = lexer_.next();
        auto right = parseNot();
        if (!right) return nullptr;
        left = Condition::binaryOp(ConditionType::And, left, right);
    }
    return left;
}

ConditionPtr ConditionParser::parseNot() {
    if (token_.type == TokenType::Identifier && token_.value == "not") {
        token_ = lexer_.next();
        auto operand = parseNot();
        if (!operand) return nullptr;
        return Condition::notOp(operand);
    }
    return parsePrimary();
}

ConditionPtr ConditionParser::parsePrimary() {
    if (token_.type == TokenType::LParen) {
        token_ = lexer_.next();
        auto inner = parseOr();
        if (!inner) return nullptr;
        if (token_.type != TokenType::RParen) {
            error(token_.line, token_.column, "Ожидалась ')'");
            return nullptr;
        }
        token_ = lexer_.next();
        return inner;
    }

    if (token_.type == TokenType::Identifier && token_.value == "read") {
        int readLine = token_.line;
        int readCol = token_.column;
        token_ = lexer_.next();

        bool isEq = false;
        if (token_.type == TokenType::EqEq) {
            isEq = true;
        } else if (token_.type == TokenType::NotEq) {
            isEq = false;
        } else {
            error(token_.line, token_.column, "После 'read' ожидалось '==' или '!='");
            return nullptr;
        }
        token_ = lexer_.next();

        if (token_.type != TokenType::StringLiteral) {
            error(token_.line, token_.column, "Ожидался символ в кавычках");
            return nullptr;
        }

        std::string symStr = token_.value;
        Symbol sym = (symStr == "blank") ? blankSymbol_ : symStr;

        if (sym != blankSymbol_ && !alphabetSet_.count(sym)) {
            error(token_.line, token_.column, "Символ '" + symStr + "' не определён в алфавите");
            return nullptr;
        }

        token_ = lexer_.next();

        if (isEq) {
            return Condition::readEq(sym, readLine, readCol);
        } else {
            return Condition::readNeq(sym, readLine, readCol);
        }
    }

    error(token_.line, token_.column, "Ожидалось условие (read == \"...\", read != \"...\", not, или '(')");
    return nullptr;
}
