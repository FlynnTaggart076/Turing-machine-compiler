#include "Condition.h"

bool containsVarCondition(const ConditionPtr& cond) {
    if (!cond) return false;
    if (cond->type == ConditionType::VarLtConst || cond->type == ConditionType::VarGtConst) return true;
    if (cond->left && containsVarCondition(cond->left)) return true;
    if (cond->right && containsVarCondition(cond->right)) return true;
    if (cond->operand && containsVarCondition(cond->operand)) return true;
    return false;
}

bool isCompoundCondition(const ConditionPtr& cond) {
    if (!cond) return false;
    return cond->type == ConditionType::And ||
           cond->type == ConditionType::Or ||
           cond->type == ConditionType::Xor ||
           cond->type == ConditionType::Not;
}

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
    case ConditionType::VarLtConst:
        // Условие Var обрабатываем отдельно в TransitionGenerator
        return false; // Заглушка
    case ConditionType::VarGtConst:
        return false; // Заглушка
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

    // Условие x < N или x > N
    if (token_.type == TokenType::Identifier && token_.value == "x") {
        int xLine = token_.line;
        int xCol = token_.column;
        token_ = lexer_.next();

        bool isLess = false;
        if (token_.type == TokenType::Less) {
            isLess = true;
        } else if (token_.type == TokenType::Greater) {
            isLess = false;
        } else {
            error(token_.line, token_.column, "После 'x' в условии ожидалось '<' или '>'");
            return nullptr;
        }
        token_ = lexer_.next();

        if (token_.type != TokenType::Number) {
            error(token_.line, token_.column, "После 'x <' или 'x >' ожидалось число");
            return nullptr;
        }

        int value = 0;
        try {
            value = std::stoi(token_.value);
        } catch (...) {
            error(token_.line, token_.column, "Некорректное число: '" + token_.value + "'");
            return nullptr;
        }

        // Проверка диапазона [-128..127]
        if (value < -128 || value > 127) {
            error(token_.line, token_.column, "Значение должно быть в диапазоне [-128..127]");
            return nullptr;
        }

        token_ = lexer_.next();
        if (isLess) {
            return Condition::varLtConst(value, xLine, xCol);
        } else {
            return Condition::varGtConst(value, xLine, xCol);
        }
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

    error(token_.line, token_.column, "Ожидалось условие (read == \"...\", read != \"...\", x < N, not, или '(')");
    return nullptr;
}
