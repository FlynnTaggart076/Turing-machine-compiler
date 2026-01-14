#pragma once

#include <string_view>
#include <vector>

#include "Diagnostics.h"
#include "TransitionTable.h"
#include "TuringMachine.h"

/**
 * @struct CompileResult
 * @brief Результат компиляции программы
 */
struct CompileResult {
    bool ok{false};
    TransitionTable table;
    std::vector<Diagnostic> diagnostics;
    std::vector<Symbol> alphabet;
    Tape initialTape;
};

/** @class Compiler
 *  @brief Компилятор языка программирования машины Тьюринга
 */
class Compiler {
public:
    /**
     * @brief Скомпилировать исходный код в таблицу переходов
     * @param source Исходный код программы
     * @return Результат компиляции с таблицей и диагностикой
     */
    CompileResult compile(std::string_view source) const;
};
