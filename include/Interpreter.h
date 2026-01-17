#pragma once

#include "TransitionTable.h"
#include "TuringMachine.h"

/** @brief Результат выполнения одного шага машины */
enum class StepResult { 
    Ok,
    Halted,
    NoTransition
};

/** @brief Исполнитель машины Тьюринга */
class Interpreter {
public:
    /** @brief Выполнить один шаг машины Тьюринга */
    StepResult step(TuringMachine& tm, const TransitionTable& table);
};
