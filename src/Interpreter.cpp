#include "Interpreter.h"

StepResult Interpreter::step(TuringMachine& tm, const TransitionTable& table) {
    // Уже остановлена
    if (tm.isHalted()) {
        return StepResult::Halted;
    }

    // Достигнуто состояние останова
    if (tm.getState() == table.haltState) {
        tm.setHalted(true);
        return StepResult::Halted;
    }

    const Symbol current = tm.read();
    
    const Transition* transition = table.get(tm.getState(), current);
    
    if (!transition) {
        tm.setHalted(true);
        return StepResult::NoTransition;
    }

    // Применить переход
    tm.write(transition->writeSymbol);
    tm.move(transition->move);
    tm.setState(transition->nextState);
    tm.setHalted(tm.getState() == table.haltState);
    return tm.isHalted() ? StepResult::Halted : StepResult::Ok;
}
