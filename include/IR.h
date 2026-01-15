#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Condition.h"

/** @brief Типы IR инструкций */
enum class IRType {
    MoveLeft,
    MoveRight,
    Write,
    Call,
    IfElse,
    While
};

struct IRInstruction;
using IRBlock = std::vector<std::shared_ptr<IRInstruction>>;

/** @brief Инструкция промежуточного представления */
struct IRInstruction {
    IRType type;
    std::string argument;
    ConditionPtr condition;
    IRBlock thenBranch;
    IRBlock elseBranch;
    int line;
    int column;

    static std::shared_ptr<IRInstruction> simple(IRType t, const std::string& arg, int l, int c);
    static std::shared_ptr<IRInstruction> ifElse(ConditionPtr cond, IRBlock thenB, IRBlock elseB, int l, int c);
    static std::shared_ptr<IRInstruction> whileLoop(ConditionPtr cond, IRBlock body, int l, int c);
};

/** @brief Процедура - именованный блок инструкций */
struct Procedure {
    std::string name;
    IRBlock body;
    int line;
    int column;
};
