#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "Diagnostics.h"
#include "Lexer.h"
#include "Types.h"

/** @brief Типы узлов дерева условий */
enum class ConditionType {
    ReadEq,
    ReadNeq,
    And,
    Or,
    Xor,
    Not
};

struct Condition;
using ConditionPtr = std::shared_ptr<Condition>;

/** @brief Узел дерева условий (AST) */
struct Condition {
    ConditionType type;
    std::string symbol;
    ConditionPtr left;
    ConditionPtr right;
    ConditionPtr operand;
    int line{0};
    int column{0};

    static ConditionPtr readEq(const std::string& sym, int l, int c) {
        auto cond = std::make_shared<Condition>();
        cond->type = ConditionType::ReadEq;
        cond->symbol = sym;
        cond->line = l;
        cond->column = c;
        return cond;
    }

    static ConditionPtr readNeq(const std::string& sym, int l, int c) {
        auto cond = std::make_shared<Condition>();
        cond->type = ConditionType::ReadNeq;
        cond->symbol = sym;
        cond->line = l;
        cond->column = c;
        return cond;
    }

    static ConditionPtr binaryOp(ConditionType t, ConditionPtr l, ConditionPtr r) {
        auto cond = std::make_shared<Condition>();
        cond->type = t;
        cond->left = l;
        cond->right = r;
        return cond;
    }

    static ConditionPtr notOp(ConditionPtr op) {
        auto cond = std::make_shared<Condition>();
        cond->type = ConditionType::Not;
        cond->operand = op;
        return cond;
    }
};

/** @brief Вычисление логического условия для текущего символа */
bool evaluateCondition(const ConditionPtr& cond, const Symbol& currentSymbol);

/** @brief Парсер булевых условий для конструкций if/while */
class ConditionParser {
public:
    ConditionParser(
        Lexer& lexer,
        Token& currentToken,
        const std::unordered_set<Symbol>& alphabetSet,
        const Symbol& blankSymbol,
        std::vector<Diagnostic>& diagnostics,
        bool& ok);

    ConditionPtr parse();

private:
    Lexer& lexer_;
    Token& token_;
    const std::unordered_set<Symbol>& alphabetSet_;
    const Symbol& blankSymbol_;
    std::vector<Diagnostic>& diagnostics_;
    bool& ok_;

    void error(int line, int col, const std::string& msg);
    ConditionPtr parseOr();
    ConditionPtr parseXor();
    ConditionPtr parseAnd();
    ConditionPtr parseNot();
    ConditionPtr parsePrimary();
};
