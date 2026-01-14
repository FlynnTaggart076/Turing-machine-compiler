#include "TuringMachine.h"

#include <utility>

// Лента

Tape::Tape(Symbol blank) : blank_(std::move(blank)) {}

Symbol Tape::get(long long position) const {
    auto it = cells_.find(position);
    if (it == cells_.end()) {
        return blank_;
    }
    return it->second;
}

std::pair<long long, long long> Tape::bounds(long long head) const {
    if (cells_.empty()) {
        return {head, head};
    }

    // Поиск минимальной и максимальной позиций непустых ячеек
    long long minPos = head;
    long long maxPos = head;
    for (const auto& kv : cells_) {
        minPos = std::min(minPos, kv.first);
        maxPos = std::max(maxPos, kv.first);
    }
    return {minPos, maxPos};
}

void Tape::set(long long position, Symbol value) {
    if (value == blank_) {
        cells_.erase(position);
        return;
    }
    cells_[position] = std::move(value);
}

void Tape::clear() {
    cells_.clear();
}

// Машина Тьюринга

TuringMachine::TuringMachine() = default;

void TuringMachine::reset(const Tape& initialTape, StateId startState) {
    tape_ = initialTape;
    head_ = 0;
    state_ = startState;
    halted_ = false;
    steps_ = 0;
}

Symbol TuringMachine::read() const {
    return tape_.get(head_);
}

void TuringMachine::write(Symbol value) {
    tape_.set(head_, value);
}

void TuringMachine::move(Move move) {
    switch (move) {
    case Move::Left:
        head_--;
        break;
    case Move::Right:
        head_++;
        break;
    case Move::Stay:
        break;
    default:
        break;
    }
}

StateId TuringMachine::getState() const {
    return state_;
}

void TuringMachine::setState(StateId state) {
    state_ = state;
}

bool TuringMachine::isHalted() const {
    return halted_;
}

void TuringMachine::setHalted(bool halted) {
    halted_ = halted;
}

long long TuringMachine::head() const {
    return head_;
}

const Tape& TuringMachine::tape() const {
    return tape_;
}

Tape& TuringMachine::tape() {
    return tape_;
}

uint64_t TuringMachine::steps() const {
    return steps_;
}
