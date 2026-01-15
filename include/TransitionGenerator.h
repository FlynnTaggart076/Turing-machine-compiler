#pragma once

#include <vector>

#include "Condition.h"
#include "IR.h"
#include "TransitionTable.h"

/** @brief Генерация таблицы переходов МТ из плоского IR */
void generateTransitions(
    const IRBlock& instructions,
    const std::vector<Symbol>& alphabet,
    TransitionTable& table);
