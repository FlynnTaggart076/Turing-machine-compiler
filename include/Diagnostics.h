#pragma once

#include <string>

/** @brief Уровень диагностического сообщения */
enum class DiagnosticLevel { Error, Warning, Info };

/** @brief Диагностическое сообщение от компилятора */
struct Diagnostic {
    DiagnosticLevel level{DiagnosticLevel::Error};
    int line{0};
    int column{0};
    std::string message;
};
