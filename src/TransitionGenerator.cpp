#include "TransitionGenerator.h"
#include "CodegenPrimitives.h"
#include "MemoryLayout.h"

using namespace MemoryLayout;

namespace {


// Смещение для L
StateId g_phaseOffset = 0;

bool isSystemSymbol(const Symbol& sym) {
    return sym == kSymBOM || sym == kSymEOM || sym == kBit0 || sym == kBit1;
}


// Рекурсивно считаем количество состояний
StateId countConditionStates(const ConditionPtr& cond, const std::vector<Symbol>& alphabet) {
    if (!cond) return 1;
    
    switch (cond->type) {
    case ConditionType::VarLtConst:
        return countCmpInt8States(alphabet, cond->intValue);
    
    case ConditionType::VarGtConst:
        return countCmpInt8States(alphabet, cond->intValue);
        
    case ConditionType::ReadEq:
    case ConditionType::ReadNeq:
        return 1;
        
    case ConditionType::And:
        return countConditionStates(cond->left, alphabet) + 
               countConditionStates(cond->right, alphabet);
        
    case ConditionType::Or:
        return countConditionStates(cond->left, alphabet) + 
               countConditionStates(cond->right, alphabet);
        
    case ConditionType::Xor:
        return countConditionStates(cond->left, alphabet) + 
               countConditionStates(cond->right, alphabet) * 2;
        
    case ConditionType::Not:
        // Просто инвертируем результат - то же количество состояний
        return countConditionStates(cond->operand, alphabet);
    }
    return 1;
}


// Генерация переходов для состовных условий

StateId generateConditionTransitions(
    const ConditionPtr& cond,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId startState,
    StateId thenState,
    StateId elseState,
    CodegenContext& ctx);

// ReadEq/ReadNeq
StateId generateReadCondition(
    const ConditionPtr& cond,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId currentState,
    StateId thenState,
    StateId elseState
) {
    for (const auto& sym : alphabet) {
        bool condTrue = (cond->type == ConditionType::ReadEq) 
                        ? (sym == cond->symbol)
                        : (sym != cond->symbol);
        if (condTrue) {
            table.add(currentState, sym, {thenState, sym, Move::Stay});
        } else {
            table.add(currentState, sym, {elseState, sym, Move::Stay});
        }
    }
    return currentState + 1;
}

// Проверка условий
StateId generateConditionTransitions(
    const ConditionPtr& cond,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId startState,
    StateId thenState,
    StateId elseState,
    CodegenContext& ctx
) {
    if (!cond) {
        // Пустое условие - просто true
        for (const auto& sym : alphabet) {
            table.add(startState, sym, {thenState, sym, Move::Stay});
        }
        return startState + 1;
    }
    
    switch (cond->type) {
    case ConditionType::VarLtConst: {
        // Уже есть для x < N
        genCmpInt8Const_LT(ctx, startState, thenState, elseState, cond->intValue);
        return startState + countCmpInt8States(alphabet, cond->intValue);
    }
    
    case ConditionType::VarGtConst: {
        // Уже есть для x > N
        genCmpInt8Const_GT(ctx, startState, thenState, elseState, cond->intValue);
        return startState + countCmpInt8States(alphabet, cond->intValue);
    }
    
    case ConditionType::ReadEq:
    case ConditionType::ReadNeq:
        return generateReadCondition(cond, alphabet, table, startState, thenState, elseState);
    
    case ConditionType::And: {
        // AND
        StateId leftStates = countConditionStates(cond->left, alphabet);
        StateId rightStart = startState + leftStates;
        
        // Левое условие
        generateConditionTransitions(cond->left, alphabet, table, startState, rightStart, elseState, ctx);
        
        // Правое условие
        return generateConditionTransitions(cond->right, alphabet, table, rightStart, thenState, elseState, ctx);
    }
    
    case ConditionType::Or: {
        // OR
        StateId leftStates = countConditionStates(cond->left, alphabet);
        StateId rightStart = startState + leftStates;
        
        // Левое условие
        generateConditionTransitions(cond->left, alphabet, table, startState, thenState, rightStart, ctx);
        
        // Правое условие
        return generateConditionTransitions(cond->right, alphabet, table, rightStart, thenState, elseState, ctx);
    }
    
    case ConditionType::Xor: {
        // XOR
        StateId leftStates = countConditionStates(cond->left, alphabet);
        StateId rightStates = countConditionStates(cond->right, alphabet);
        
        StateId rightIfLeftTrue = startState + leftStates;
        StateId rightIfLeftFalse = rightIfLeftTrue + rightStates;
        
        // Левое условие
        generateConditionTransitions(cond->left, alphabet, table, startState, rightIfLeftTrue, rightIfLeftFalse, ctx);
        
        // Левое оказалось true
        generateConditionTransitions(cond->right, alphabet, table, rightIfLeftTrue, elseState, thenState, ctx);
        
        // Левое оказалось false
        return generateConditionTransitions(cond->right, alphabet, table, rightIfLeftFalse, thenState, elseState, ctx);
    }
    
    case ConditionType::Not:
        // NOT - просто меняем then и else местами
        return generateConditionTransitions(cond->operand, alphabet, table, startState, elseState, thenState, ctx);
    }
    
    return startState + 1;
}




constexpr StateId kSkipMemoryStates = 10;

StateId countStates(const IRBlock& block, const std::vector<Symbol>& alphabet);

StateId countInstructionStates(const std::shared_ptr<IRInstruction>& instr, const std::vector<Symbol>& alphabet) {
    if (instr->type == IRType::MoveLeft || instr->type == IRType::MoveRight) {
        return 2 + kSkipMemoryStates;
    }
    
    if (instr->type == IRType::IfElse) {
        StateId thenStates = countStates(instr->thenBranch, alphabet);
        StateId elseStates = countStates(instr->elseBranch, alphabet);
        
        StateId condStates = countConditionStates(instr->condition, alphabet);
        return thenStates + elseStates + condStates;
    }
    if (instr->type == IRType::While) {
        StateId bodyStates = countStates(instr->thenBranch, alphabet);
        
        StateId condStates = countConditionStates(instr->condition, alphabet);
        return bodyStates + condStates;
    }
    
    if (instr->type == IRType::VarSetConst) {
        return countVarSetConstStates(alphabet);
    }
    if (instr->type == IRType::VarInc) {
        return countVarIncStates(alphabet);
    }
    if (instr->type == IRType::VarDec) {
        return countVarDecStates(alphabet);
    }
    
    return 1;
}

StateId countStates(const IRBlock& block, const std::vector<Symbol>& alphabet) {
    StateId count = 0;
    for (const auto& instr : block) {
        count += countInstructionStates(instr, alphabet);
    }
    return count;
}

StateId generateBlockTransitions(
    const IRBlock& block,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId startState,
    StateId exitState,
    bool phaseR);

StateId generateSkipMemoryLeft(
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId startState,
    StateId exitStateL
) {
    StateId s = startState;
    for (int i = 0; i < 9; i++) {
        StateId nextS = s + 1;
        for (const auto& sym : alphabet) {
            table.add(s, sym, {nextS, sym, Move::Left});
        }
        s = nextS;
    }
    
    // Последний шаг влево - переходим в фазу L
    for (const auto& sym : alphabet) {
        table.add(s, sym, {exitStateL, sym, Move::Left});
    }
    return s + 1;
}

StateId generateSkipMemoryRight(
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId startState,
    StateId exitStateR
) {
    StateId s = startState;
    for (int i = 0; i < 9; i++) {
        StateId nextS = s + 1;
        for (const auto& sym : alphabet) {
            table.add(s, sym, {nextS, sym, Move::Right});
        }
        s = nextS;
    }
    // Последний шаг вправо - переходим в фазу R
    for (const auto& sym : alphabet) {
        table.add(s, sym, {exitStateR, sym, Move::Right});
    }
    return s + 1;
}

StateId generateInstructionTransitions(
    const std::shared_ptr<IRInstruction>& instr,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId currentState,
    StateId nextState,
    bool phaseR
) {
    StateId nextStateR = phaseR ? nextState : (nextState - g_phaseOffset);
    StateId nextStateL = phaseR ? (nextState + g_phaseOffset) : nextState;
    
    switch (instr->type) {
    case IRType::MoveLeft: {
        StateId afterMove = currentState + 1;
        StateId skipStart = currentState + 2;
        
        if (phaseR) {
            for (const auto& sym : alphabet) {
                table.add(currentState, sym, {afterMove, sym, Move::Left});
            }
            
            for (const auto& sym : alphabet) {
                if (sym == kSymEOM) {
                    table.add(afterMove, sym, {skipStart, sym, Move::Stay});
                } else if (isSystemSymbol(sym)) {
                    table.add(afterMove, sym, {nextStateR, sym, Move::Stay});
                } else {
                    table.add(afterMove, sym, {nextStateR, sym, Move::Stay});
                }
            }
            generateSkipMemoryLeft(alphabet, table, skipStart, nextStateL);
        } else {
            for (const auto& sym : alphabet) {
                table.add(currentState, sym, {nextStateL, sym, Move::Left});
            }
        }
        return skipStart + kSkipMemoryStates;
    }

    case IRType::MoveRight: {
        StateId afterMove = currentState + 1;
        StateId skipStart = currentState + 2;
        
        if (phaseR) {
            for (const auto& sym : alphabet) {
                table.add(currentState, sym, {nextStateR, sym, Move::Right});
            }
        } else {
            for (const auto& sym : alphabet) {
                table.add(currentState, sym, {afterMove, sym, Move::Right});
            }
            
            for (const auto& sym : alphabet) {
                if (sym == kSymBOM) {
                    table.add(afterMove, sym, {skipStart, sym, Move::Stay});
                } else if (isSystemSymbol(sym)) {
                    table.add(afterMove, sym, {nextStateL, sym, Move::Stay});
                } else {
                    table.add(afterMove, sym, {nextStateL, sym, Move::Stay});
                }
            }
            generateSkipMemoryRight(alphabet, table, skipStart, nextStateR);
        }
        return skipStart + kSkipMemoryStates;
    }

    case IRType::Write:
        for (const auto& sym : alphabet) {
            table.add(currentState, sym, {nextState, instr->argument, Move::Stay});
        }
        return nextState;

    case IRType::Call:
        return nextState;

    case IRType::VarSetConst: {
        CodegenContext ctx;
        ctx.tt = &table;
        ctx.nextState = currentState + 1;
        ctx.alphabet = alphabet;
        ctx.phaseR = phaseR;
        
        genSetInt8Const(ctx, currentState, nextState, instr->intValue);
        return nextState;
    }

    case IRType::VarInc: {
        CodegenContext ctx;
        ctx.tt = &table;
        ctx.nextState = currentState + 1;
        ctx.alphabet = alphabet;
        ctx.phaseR = phaseR;
        
        genIncInt8(ctx, currentState, nextState);
        return nextState;
    }

    case IRType::VarDec: {
        CodegenContext ctx;
        ctx.tt = &table;
        ctx.nextState = currentState + 1;
        ctx.alphabet = alphabet;
        ctx.phaseR = phaseR;
        
        genDecInt8(ctx, currentState, nextState);
        return nextState;
    }

    case IRType::IfElse: {
        StateId thenStates = countStates(instr->thenBranch, alphabet);
        StateId elseStates = countStates(instr->elseBranch, alphabet);
        StateId condStates = countConditionStates(instr->condition, alphabet);

        StateId thenStart = currentState + condStates;
        StateId elseStart = thenStart + thenStates;
        
        StateId thenTarget = (thenStates > 0) ? thenStart : nextState;
        StateId elseTarget = (elseStates > 0) ? elseStart : nextState;
        
        CodegenContext ctx;
        ctx.tt = &table;
        ctx.nextState = currentState + 1;
        ctx.alphabet = alphabet;
        ctx.phaseR = phaseR;
        
        generateConditionTransitions(instr->condition, alphabet, table, currentState, thenTarget, elseTarget, ctx);
        
        if (thenStates > 0) {
            generateBlockTransitions(instr->thenBranch, alphabet, table, thenStart, nextState, phaseR);
        }
        if (elseStates > 0) {
            generateBlockTransitions(instr->elseBranch, alphabet, table, elseStart, nextState, phaseR);
        }
        
        return elseStart + elseStates;
    }

    case IRType::While: {
        StateId bodyStates = countStates(instr->thenBranch, alphabet);
        StateId condStates = countConditionStates(instr->condition, alphabet);
        
        StateId bodyStart = currentState + condStates;
        StateId bodyTarget = (bodyStates > 0) ? bodyStart : currentState;
        
        CodegenContext ctx;
        ctx.tt = &table;
        ctx.nextState = currentState + 1;
        ctx.alphabet = alphabet;
        ctx.phaseR = phaseR;
        
        generateConditionTransitions(instr->condition, alphabet, table, currentState, bodyTarget, nextState, ctx);
        
        if (bodyStates > 0) {
            generateBlockTransitions(instr->thenBranch, alphabet, table, bodyStart, currentState, phaseR);
        }
        
        return bodyStart + bodyStates;
    }
    }
    return nextState;
}

StateId generateBlockTransitions(
    const IRBlock& block,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId startState,
    StateId exitState,
    bool phaseR
) {
    if (block.empty()) {
        return startState;
    }

    StateId current = startState;
    for (std::size_t i = 0; i < block.size(); i++) {
        const auto& instr = block[i];
        StateId statesNeeded = countInstructionStates(instr, alphabet);
        StateId next = (i + 1 < block.size()) ? (current + statesNeeded) : exitState;
        generateInstructionTransitions(instr, alphabet, table, current, next, phaseR);
        current += statesNeeded;
    }
    return current;
}

} // namespace

void generateTransitions(
    const IRBlock& instructions,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table
) {
    StateId singlePhaseStates = countStates(instructions, alphabet);
    
    g_phaseOffset = singlePhaseStates + 1;
    
    const StateId haltStateR = singlePhaseStates;
    const StateId haltStateL = g_phaseOffset + singlePhaseStates;

    table.startState = 0;
    table.haltState = haltStateR;

    if (instructions.empty()) {
        table.startState = 0;
        table.haltState = 0;
        return;
    }

    generateBlockTransitions(instructions, alphabet, table, 0, haltStateR, true);
    
    generateBlockTransitions(instructions, alphabet, table, g_phaseOffset, haltStateL, false);
    
    for (const auto& sym : alphabet) {
        table.add(haltStateL, sym, {haltStateR, sym, Move::Stay});
    }
}
