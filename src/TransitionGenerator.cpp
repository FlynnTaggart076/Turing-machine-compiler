#include "TransitionGenerator.h"

namespace {

StateId countStates(const IRBlock& block, const std::vector<Symbol>& alphabet);

StateId countInstructionStates(const std::shared_ptr<IRInstruction>& instr, const std::vector<Symbol>& alphabet) {
    if (instr->type == IRType::IfElse) {
        StateId thenStates = countStates(instr->thenBranch, alphabet);
        StateId elseStates = countStates(instr->elseBranch, alphabet);
        return thenStates + elseStates + 1;
    }
    if (instr->type == IRType::While) {
        StateId bodyStates = countStates(instr->thenBranch, alphabet);
        return bodyStates + 1;
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
    StateId exitState);

StateId generateInstructionTransitions(
    const std::shared_ptr<IRInstruction>& instr,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table,
    StateId currentState,
    StateId nextState
) {
    switch (instr->type) {
    case IRType::MoveLeft:
        for (const auto& sym : alphabet) {
            table.add(currentState, sym, {nextState, sym, Move::Left});
        }
        return nextState;

    case IRType::MoveRight:
        for (const auto& sym : alphabet) {
            table.add(currentState, sym, {nextState, sym, Move::Right});
        }
        return nextState;

    case IRType::Write:
        for (const auto& sym : alphabet) {
            table.add(currentState, sym, {nextState, instr->argument, Move::Stay});
        }
        return nextState;

    case IRType::Call:
        return nextState;

    case IRType::IfElse: {
        StateId thenStates = countStates(instr->thenBranch, alphabet);
        StateId elseStates = countStates(instr->elseBranch, alphabet);

        StateId thenStart = currentState + 1;
        StateId elseStart = thenStart + thenStates;
        StateId afterElse = elseStart + elseStates;

        StateId branchExit = (thenStates == 0 && elseStates == 0) ? nextState : afterElse;
        if (branchExit > nextState) {
            branchExit = nextState;
        }

        for (const auto& sym : alphabet) {
            bool condTrue = evaluateCondition(instr->condition, sym);
            if (condTrue) {
                StateId target = (thenStates > 0) ? thenStart : nextState;
                table.add(currentState, sym, {target, sym, Move::Stay});
            } else {
                StateId target = (elseStates > 0) ? elseStart : nextState;
                table.add(currentState, sym, {target, sym, Move::Stay});
            }
        }

        if (thenStates > 0) {
            generateBlockTransitions(instr->thenBranch, alphabet, table, thenStart, nextState);
        }

        if (elseStates > 0) {
            generateBlockTransitions(instr->elseBranch, alphabet, table, elseStart, nextState);
        }

        return afterElse;
    }

    case IRType::While: {
        StateId bodyStates = countStates(instr->thenBranch, alphabet);
        StateId bodyStart = currentState + 1;

        for (const auto& sym : alphabet) {
            bool condTrue = evaluateCondition(instr->condition, sym);
            if (condTrue) {
                StateId target = (bodyStates > 0) ? bodyStart : currentState;
                table.add(currentState, sym, {target, sym, Move::Stay});
            } else {
                table.add(currentState, sym, {nextState, sym, Move::Stay});
            }
        }

        if (bodyStates > 0) {
            generateBlockTransitions(instr->thenBranch, alphabet, table, bodyStart, currentState);
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
    StateId exitState
) {
    if (block.empty()) {
        return startState;
    }

    StateId current = startState;
    for (std::size_t i = 0; i < block.size(); i++) {
        const auto& instr = block[i];
        StateId statesNeeded = countInstructionStates(instr, alphabet);
        StateId next = (i + 1 < block.size()) ? (current + statesNeeded) : exitState;
        generateInstructionTransitions(instr, alphabet, table, current, next);
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
    StateId totalStates = countStates(instructions, alphabet);
    const StateId haltState = totalStates;

    table.startState = 0;
    table.haltState = haltState;

    if (instructions.empty()) {
        table.startState = 0;
        table.haltState = 0;
        return;
    }

    generateBlockTransitions(instructions, alphabet, table, 0, haltState);
}
