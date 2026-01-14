#include "TransitionTable.h"

#include <algorithm>
#include <utility>

bool TransitionTable::add(StateId state, Symbol symbol, const Transition& transition) {
    Key key{state, symbol};
    
    // Детерминированность: только один переход на пару (состояние, символ).
    auto [it, inserted] = transitions_.emplace(key, transition);
    
    if (!inserted) {
        return false;
    }
    return true;
}

bool TransitionTable::has(StateId state, Symbol symbol) const {
    Key key{state, symbol};
    return transitions_.find(key) != transitions_.end();
}

const Transition* TransitionTable::get(StateId state, Symbol symbol) const {
    Key key{state, symbol};
    auto it = transitions_.find(key);
    if (it == transitions_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<StateId> TransitionTable::states() const {
    std::unordered_set<StateId> s;
    
    s.insert(startState);
    s.insert(haltState);
    
    for (const auto& kv : transitions_) {
        s.insert(kv.first.state);
        s.insert(kv.second.nextState);
    }
    
    std::vector<StateId> out(s.begin(), s.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Symbol> TransitionTable::alphabet() const {
    std::unordered_set<Symbol> a;
    
    for (const auto& kv : transitions_) {
        a.insert(kv.first.symbol);
        a.insert(kv.second.writeSymbol);
    }
    
    std::vector<Symbol> out(a.begin(), a.end());
    std::sort(out.begin(), out.end());
    return out;
}

bool TransitionTable::validate(std::vector<Diagnostic>& out) const {
    bool ok = true;

    if (startState == haltState) {
        ok = false;
        out.push_back({DiagnosticLevel::Error, 0, 0, "startState совпадает с haltState"});
    }

    return ok;
}
