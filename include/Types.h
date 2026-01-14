#pragma once

#include <cstdint>
#include <string>

/** @brief Идентификатор состояния машины Тьюринга */
using StateId = int;

/** @brief Символ алфавита ленты машины Тьюринга */
using Symbol = std::string;

/** @brief Направление движения головки */
enum class Move { Left, Right, Stay };
