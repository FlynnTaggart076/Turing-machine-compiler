#include "IR.h"

std::shared_ptr<IRInstruction> IRInstruction::simple(IRType t, const std::string& arg, int l, int c) {
    auto instr = std::make_shared<IRInstruction>();
    instr->type = t;
    instr->argument = arg;
    instr->line = l;
    instr->column = c;
    return instr;
}

std::shared_ptr<IRInstruction> IRInstruction::ifElse(ConditionPtr cond, IRBlock thenB, IRBlock elseB, int l, int c) {
    auto instr = std::make_shared<IRInstruction>();
    instr->type = IRType::IfElse;
    instr->condition = cond;
    instr->thenBranch = std::move(thenB);
    instr->elseBranch = std::move(elseB);
    instr->line = l;
    instr->column = c;
    return instr;
}

std::shared_ptr<IRInstruction> IRInstruction::whileLoop(ConditionPtr cond, IRBlock body, int l, int c) {
    auto instr = std::make_shared<IRInstruction>();
    instr->type = IRType::While;
    instr->condition = cond;
    instr->thenBranch = std::move(body);
    instr->line = l;
    instr->column = c;
    return instr;
}

std::shared_ptr<IRInstruction> IRInstruction::varSetConst(int value, int l, int c) {
    auto instr = std::make_shared<IRInstruction>();
    instr->type = IRType::VarSetConst;
    instr->intValue = value;
    instr->line = l;
    instr->column = c;
    return instr;
}

std::shared_ptr<IRInstruction> IRInstruction::varInc(int l, int c) {
    auto instr = std::make_shared<IRInstruction>();
    instr->type = IRType::VarInc;
    instr->line = l;
    instr->column = c;
    return instr;
}

std::shared_ptr<IRInstruction> IRInstruction::varDec(int l, int c) {
    auto instr = std::make_shared<IRInstruction>();
    instr->type = IRType::VarDec;
    instr->line = l;
    instr->column = c;
    return instr;
}
