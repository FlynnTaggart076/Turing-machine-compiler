#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Diagnostics.h"
#include "IR.h"

/** @brief Разворачивает вызовы процедур (без рекурсии) в плоский IR */
bool flattenProcedure(
    const std::string& procName,
    const std::unordered_map<std::string, Procedure>& procedures,
    IRBlock& output,
    std::unordered_set<std::string>& callStack,
    std::vector<Diagnostic>& diagnostics);
