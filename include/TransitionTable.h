#pragma once

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

#include "Diagnostics.h"
#include "Types.h"

/** @brief Одно правило перехода машины Тьюринга */
struct Transition {
    StateId nextState{0};
    Symbol writeSymbol{" "};
    Move move{Move::Stay};
};

/** @brief Таблица переходов (программа) машины Тьюринга */
class TransitionTable {
public:
    StateId startState{0};
    StateId haltState{1};

    /** @brief Добавить правило перехода */
    bool add(StateId state, Symbol symbol, const Transition& transition);

    /** @brief Проверить наличие перехода */
    bool has(StateId state, Symbol symbol) const;

    /** @brief Получить переход (nullptr если не найден) */
    const Transition* get(StateId state, Symbol symbol) const;

    /** @brief Получить все состояния */
    std::vector<StateId> states() const;

    /** @brief Получить алфавит */
    std::vector<Symbol> alphabet() const;

    /** @brief Проверить корректность таблицы */
    bool validate(std::vector<Diagnostic>& out) const;

private:
    struct Key {
        StateId state;
        Symbol symbol;
        bool operator==(const Key& other) const {
            return state == other.state && symbol == other.symbol;
        }
    };

    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            const std::size_t hs = std::hash<StateId>{}(key.state);
            const std::size_t hsym = std::hash<Symbol>{}(key.symbol);
            return hs ^ (hsym << 1);
        }
    };

    std::unordered_map<Key, Transition, KeyHash> transitions_;
};
