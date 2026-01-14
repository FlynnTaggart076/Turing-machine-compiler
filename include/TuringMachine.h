#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>

#include "Types.h"

/** @brief Модель бесконечной ленты машины Тьюринга */
class Tape {
public:
    explicit Tape(Symbol blank = " ");

    /** @brief Прочитать символ в позиции */
    Symbol get(long long position) const;

    /** @brief Записать символ в позицию */
    void set(long long position, Symbol value);

    /** @brief Очистить ленту */
    void clear();

    /** @brief Получить границы записанного содержимого */
    std::pair<long long, long long> bounds(long long head) const;

    /** @brief Получить символ пустой ячейки */
    Symbol blank() const { return blank_; }

private:
    Symbol blank_;
    std::unordered_map<long long, Symbol> cells_;
};

/** @brief Полная конфигурация машины Тьюринга */
class TuringMachine {
public:
    TuringMachine();

    /** @brief Сбросить машину в начальное состояние */
    void reset(const Tape& initialTape, StateId startState);

    /** @brief Прочитать символ под головкой */
    Symbol read() const;

    /** @brief Записать символ в текущую позицию */
    void write(Symbol value);

    /** @brief Переместить головку */
    void move(Move move);

    /** @brief Получить текущее состояние */
    StateId getState() const;

    /** @brief Установить состояние */
    void setState(StateId state);

    /** @brief Проверить, остановлена ли машина */
    bool isHalted() const;

    /** @brief Установить флаг остановки */
    void setHalted(bool halted);

    /** @brief Получить позицию головки */
    long long head() const;

    /** @brief Получить ленту (только чтение) */
    const Tape& tape() const;

    /** @brief Получить ленту (для изменения) */
    Tape& tape();

    /** @brief Получить количество шагов */
    uint64_t steps() const;

private:
    Tape tape_;
    long long head_{0};
    StateId state_{0};
    bool halted_{true};
    uint64_t steps_{0};
};
