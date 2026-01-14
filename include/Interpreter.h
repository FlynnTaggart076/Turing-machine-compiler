#pragma once

#include "TransitionTable.h"
#include "TuringMachine.h"

/** @brief Результат выполнения одного шага машины */
enum class StepResult { 
    Ok,           ///< Шаг выполнен успешно
    Halted,       ///< Машина достигла состояния останова
    NoTransition  ///< Нет перехода для текущей пары (state, symbol)
};

/** @brief Исполнитель машины Тьюринга */
class Interpreter {
public:
    /** @brief Выполнить один шаг машины Тьюринга */
    StepResult step(TuringMachine& tm, const TransitionTable& table);
};
