#pragma once

#include <vector>

#include "MemoryLayout.h"
#include "TransitionTable.h"
#include "Types.h"

/** @brief Контекст генерации кода */
struct CodegenContext {
    TransitionTable* tt;              // Таблица переходов
    StateId nextState;                // Следующий свободный ID
    std::vector<Symbol> alphabet;     // Алфавит (включая системные)
    bool phaseR = true;               // Фаза: true=справа, false=слева
    
    StateId allocState() { return nextState++; }
    StateId allocStates(int n) { StateId f = nextState; nextState += n; return f; }
};

// Базовые генераторы

StateId genMoveLeftAll(CodegenContext& ctx, StateId from, StateId to);
StateId genMoveRightAll(CodegenContext& ctx, StateId from, StateId to);
StateId genStayAll(CodegenContext& ctx, StateId from, StateId to);
StateId genWriteConstAll(CodegenContext& ctx, StateId from, StateId to, const Symbol& w);
StateId genBranchOnSymbol(CodegenContext& ctx, StateId from,
                          const Symbol& match, StateId ifEq, StateId ifNeq);

// Навигация

StateId genGoToBOM(CodegenContext& ctx, StateId entry, StateId exit);
StateId genGoToEOM(CodegenContext& ctx, StateId entry, StateId exit);
StateId genGotoBitCell(CodegenContext& ctx, StateId entry, StateId exit, int bitIndex);
StateId genReturnToUserZone(CodegenContext& ctx, StateId entry, StateId exit);

// Операции с переменной (8-bit two's complement)

StateId genSetInt8Const(CodegenContext& ctx, StateId entry, StateId exit, int value);
StateId genIncInt8(CodegenContext& ctx, StateId entry, StateId exit);
StateId genDecInt8(CodegenContext& ctx, StateId entry, StateId exit);
StateId genCmpInt8Const_LT(CodegenContext& ctx, StateId entry, 
                           StateId ifTrue, StateId ifFalse, int rhs);
StateId genCmpInt8Const_GT(CodegenContext& ctx, StateId entry, 
                           StateId ifTrue, StateId ifFalse, int rhs);

// Вспомогательные

void int8ToBits(int value, Symbol bits[8]);
StateId countVarSetConstStates(const std::vector<Symbol>& alphabet);
StateId countVarIncStates(const std::vector<Symbol>& alphabet);
StateId countVarDecStates(const std::vector<Symbol>& alphabet);
StateId countCmpInt8States(const std::vector<Symbol>& alphabet, int rhs);
