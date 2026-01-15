#include "Flatten.h"

namespace {
bool flattenBlock(
    const IRBlock& block,
    const std::unordered_map<std::string, Procedure>& procedures,
    IRBlock& output,
    std::unordered_set<std::string>& callStack,
    std::vector<Diagnostic>& diagnostics);
}

bool flattenProcedure(
    const std::string& procName,
    const std::unordered_map<std::string, Procedure>& procedures,
    IRBlock& output,
    std::unordered_set<std::string>& callStack,
    std::vector<Diagnostic>& diagnostics
) {
    if (callStack.count(procName)) {
        diagnostics.push_back({DiagnosticLevel::Error, 0, 0,
            "Рекурсия не поддерживается при использовании call с возвратом (процедура '" + procName + "' вызывает себя)"});
        return false;
    }

    auto it = procedures.find(procName);
    if (it == procedures.end()) {
        diagnostics.push_back({DiagnosticLevel::Error, 0, 0,
            "Процедура '" + procName + "' не найдена"});
        return false;
    }

    callStack.insert(procName);
    bool ok = flattenBlock(it->second.body, procedures, output, callStack, diagnostics);
    callStack.erase(procName);
    return ok;
}

namespace {
bool flattenBlock(
    const IRBlock& block,
    const std::unordered_map<std::string, Procedure>& procedures,
    IRBlock& output,
    std::unordered_set<std::string>& callStack,
    std::vector<Diagnostic>& diagnostics
) {
    for (const auto& instr : block) {
        if (instr->type == IRType::Call) {
            if (!flattenProcedure(instr->argument, procedures, output, callStack, diagnostics)) {
                return false;
            }
        } else if (instr->type == IRType::IfElse) {
            IRBlock flatThen, flatElse;
            if (!flattenBlock(instr->thenBranch, procedures, flatThen, callStack, diagnostics)) {
                return false;
            }
            if (!flattenBlock(instr->elseBranch, procedures, flatElse, callStack, diagnostics)) {
                return false;
            }
            output.push_back(IRInstruction::ifElse(instr->condition, flatThen, flatElse, instr->line, instr->column));
        } else if (instr->type == IRType::While) {
            IRBlock flatBody;
            if (!flattenBlock(instr->thenBranch, procedures, flatBody, callStack, diagnostics)) {
                return false;
            }
            output.push_back(IRInstruction::whileLoop(instr->condition, flatBody, instr->line, instr->column));
        } else {
            output.push_back(instr);
        }
    }
    return true;
}
} // namespace
