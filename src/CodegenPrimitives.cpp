#include "CodegenPrimitives.h"

#include <cstdint>

using namespace MemoryLayout;


// Вспомогательные функции

void int8ToBits(int value, Symbol bits[8]) {
    // uint8_t для корректной работы с отрицательными числами
    uint8_t uval = static_cast<uint8_t>(value);
    for (int i = 0; i < 8; i++) {
        bits[i] = (uval & (1 << (7 - i))) ? kBit1 : kBit0;
    }
}


// Базовые генераторы переходов

StateId genMoveLeftAll(CodegenContext& ctx, StateId from, StateId to) {
    for (const auto& sym : ctx.alphabet) {
        ctx.tt->add(from, sym, {to, sym, Move::Left});
    }
    return to;
}

StateId genMoveRightAll(CodegenContext& ctx, StateId from, StateId to) {
    for (const auto& sym : ctx.alphabet) {
        ctx.tt->add(from, sym, {to, sym, Move::Right});
    }
    return to;
}

StateId genStayAll(CodegenContext& ctx, StateId from, StateId to) {
    for (const auto& sym : ctx.alphabet) {
        ctx.tt->add(from, sym, {to, sym, Move::Stay});
    }
    return to;
}

StateId genWriteConstAll(CodegenContext& ctx, StateId from, StateId to, const Symbol& w) {
    for (const auto& sym : ctx.alphabet) {
        ctx.tt->add(from, sym, {to, w, Move::Stay});
    }
    return to;
}

StateId genBranchOnSymbol(CodegenContext& ctx, StateId from,
                          const Symbol& match, StateId ifEq, StateId ifNeq) {
    for (const auto& sym : ctx.alphabet) {
        if (sym == match) {
            ctx.tt->add(from, sym, {ifEq, sym, Move::Stay});
        } else {
            ctx.tt->add(from, sym, {ifNeq, sym, Move::Stay});
        }
    }
    return ifNeq;
}

// Навигационные примитивы
// Идёт к памяти с учётом фазы:
// - Фаза R (справа): идём влево до EOM
// - Фаза L (слева): идём вправо до BOM

StateId genGoToMemoryEdge(CodegenContext& ctx, StateId entry, StateId exit) {
    if (ctx.phaseR) {
        // Фаза R: идём влево до EOM
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymEOM) {
                ctx.tt->add(entry, sym, {exit, sym, Move::Stay});
            } else {
                ctx.tt->add(entry, sym, {entry, sym, Move::Left});
            }
        }
    } else {
        // Фаза L: идём вправо до BOM
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymBOM) {
                ctx.tt->add(entry, sym, {exit, sym, Move::Stay});
            } else {
                ctx.tt->add(entry, sym, {entry, sym, Move::Right});
            }
        }
    }
    return 1;
}

StateId genGoToBOM(CodegenContext& ctx, StateId entry, StateId exit) {
    // Состояние entry: проверяем текущий символ
    
    for (const auto& sym : ctx.alphabet) {
        if (sym == kSymBOM) {
            ctx.tt->add(entry, sym, {exit, sym, Move::Stay});   // Если BOM - переходим в exit (stay)
        } else {
            ctx.tt->add(entry, sym, {entry, sym, Move::Left});  // Иначе - move_left и возвращаемся в entry (цикл)
        }
    }
    return 1; // Использовано 1 состояние (entry)
}

StateId genGoToEOM(CodegenContext& ctx, StateId entry, StateId exit) {
    // Состояние entry: проверяем текущий символ
    
    for (const auto& sym : ctx.alphabet) {
        if (sym == kSymEOM) {
            ctx.tt->add(entry, sym, {exit, sym, Move::Stay});  // Если EOM - переходим в exit (stay)
        } else {
            ctx.tt->add(entry, sym, {entry, sym, Move::Left}); // Иначе - move_left и возвращаемся в entry (цикл)
        }
    }
    return 1; // Использовано 1 состояние (entry)
}

StateId genGotoBitCell(CodegenContext& ctx, StateId entry, StateId exit, int bitIndex) {
    // Идём к BOM, затем делаем нужное количество шагов вправо
    
    int steps = stepsFromBOMToBit(bitIndex);
    
    // Состояние для перехода к BOM
    StateId bomState = entry;
    StateId afterBOM = ctx.allocState(); // Первое состояние после достижения BOM
    
    genGoToBOM(ctx, bomState, afterBOM);
    
    // Делаем steps шагов вправо
    StateId current = afterBOM;
    for (int i = 0; i < steps - 1; i++) {
        StateId next = ctx.allocState();
        genMoveRightAll(ctx, current, next);
        current = next;
    }
    
    // Последний шаг - в exit
    genMoveRightAll(ctx, current, exit);
    
    return 1 + steps; // Возвращаем количество использованных состояний
}

StateId genReturnToUserZone(CodegenContext& ctx, StateId entry, StateId exit) {
    StateId findEOM = entry;
    StateId afterEOM = ctx.allocState();
    
    for (const auto& sym : ctx.alphabet) {
        if (sym == kSymEOM) {
            ctx.tt->add(findEOM, sym, {afterEOM, sym, Move::Right});
        } else {
            ctx.tt->add(findEOM, sym, {findEOM, sym, Move::Right});
        }
    }
    
    StateId searchMarker = afterEOM;
    
    for (const auto& sym : ctx.alphabet) {
        if (sym == kPosMarker) {
            // Нашли маркер - заменяем на blank и выходим
            ctx.tt->add(searchMarker, sym, {exit, " ", Move::Stay});
        } else if (sym == " ") {
            // Blank без маркера - остаёмся здесь
            ctx.tt->add(searchMarker, sym, {exit, sym, Move::Stay});
        } else {
            // Другой символ - продолжаем искать маркер вправо
            ctx.tt->add(searchMarker, sym, {searchMarker, sym, Move::Right});
        }
    }
    
    return 3; // findEOM + afterEOM/searchMarker + exit
}

/** @brief Возврат к маркеру позиции с восстановлением конкретного символа */
StateId genReturnToMarker(CodegenContext& ctx, StateId entry, StateId exit, const Symbol& originalSym) {
    if (ctx.phaseR) {
        // Фаза R: память слева, маркер справа
        // Идём вправо до EOM, затем ищем маркер вправо
        
        StateId findEOM = entry;
        StateId afterEOM = ctx.allocState();
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymEOM) {
                ctx.tt->add(findEOM, sym, {afterEOM, sym, Move::Right});
            } else {
                ctx.tt->add(findEOM, sym, {findEOM, sym, Move::Right});
            }
        }
        
        StateId searchMarker = afterEOM;
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kPosMarker) {
                ctx.tt->add(searchMarker, sym, {exit, originalSym, Move::Stay});
            } else if (sym == " ") {
                ctx.tt->add(searchMarker, sym, {exit, sym, Move::Stay});
            } else {
                ctx.tt->add(searchMarker, sym, {searchMarker, sym, Move::Right});
            }
        }
    } else {
        // Фаза L: память справа, маркер слева
        // Идём влево до BOM, затем ищем маркер влево
        
        StateId findBOM = entry;
        StateId afterBOM = ctx.allocState();
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymBOM) {
                ctx.tt->add(findBOM, sym, {afterBOM, sym, Move::Left});
            } else {
                ctx.tt->add(findBOM, sym, {findBOM, sym, Move::Left});
            }
        }
        
        StateId searchMarker = afterBOM;
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kPosMarker) {
                ctx.tt->add(searchMarker, sym, {exit, originalSym, Move::Stay});
            } else if (sym == " ") {
                ctx.tt->add(searchMarker, sym, {exit, sym, Move::Stay});
            } else {
                ctx.tt->add(searchMarker, sym, {searchMarker, sym, Move::Left});
            }
        }
    }
    
    return 3;
}

// Операции с 8-битной переменной

/**
 * Для возврата на исходную позицию используем маркер '#' (системный).
 * При этом исходный символ запоминается через разветвление состояний -
 * для каждого символа алфавита создаётся своя ветка с уникальным returnState,
 * который восстанавливает именно этот символ. Да, состояний теперь дохрена,
 * но оно работает, а как иначе - хз.
 * 
 * Фазо-зависимая логика:
 * - Фаза R (справа от памяти): идём влево к BOM, пишем биты слева направо
 * - Фаза L (слева от памяти): идём вправо к BOM, пишем биты слева направо
 */

StateId genSetInt8Const(CodegenContext& ctx, StateId entry, StateId exit, int value) {
    Symbol bits[8];
    int8ToBits(value, bits);
    
    std::vector<Symbol> userSymbols;
    for (const auto& sym : ctx.alphabet) {
        if (sym != kPosMarker && sym != kSymBOM && sym != kSymEOM && 
            sym != kBit0 && sym != kBit1) {
            userSymbols.push_back(sym);
        }
    }
    
    for (const auto& origSym : userSymbols) {
        StateId afterMarker = ctx.allocState();
        ctx.tt->add(entry, origSym, {afterMarker, kPosMarker, Move::Stay});
        
        StateId goToMem = afterMarker;
        StateId afterBOM = ctx.allocState();
        
        // Идём к BOM (в фазе R - влево, в фазе L - вправо)
        Move dirToMem = ctx.phaseR ? Move::Left : Move::Right;
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymBOM) {
                ctx.tt->add(goToMem, sym, {afterBOM, sym, Move::Stay});
            } else {
                ctx.tt->add(goToMem, sym, {goToMem, sym, dirToMem});
            }
        }
        
        StateId current = afterBOM;
        
        // Пишем биты от MSB к LSB (всегда вправо)
        for (int i = 0; i < kMemBits; i++) {
            StateId onBit = ctx.allocState();
            genMoveRightAll(ctx, current, onBit);
            
            StateId afterWrite = ctx.allocState();
            genWriteConstAll(ctx, onBit, afterWrite, bits[i]);
            
            current = afterWrite;
        }
        
        genReturnToMarker(ctx, current, exit, origSym);
    }
    
    for (const auto& sym : ctx.alphabet) {
        if (sym == kPosMarker || sym == kSymBOM || sym == kSymEOM ||
            sym == kBit0 || sym == kBit1) {
            ctx.tt->add(entry, sym, {exit, sym, Move::Stay});
        }
    }
    
    return userSymbols.size() * (1 + 1 + kMemBits * 2 + 3);
}

StateId genIncInt8(CodegenContext& ctx, StateId entry, StateId exit) {
    // Инкремент x++ (инкрементируем от LSB к MSB)
    
    std::vector<Symbol> userSymbols;
    for (const auto& sym : ctx.alphabet) {
        if (sym != kPosMarker && sym != kSymBOM && sym != kSymEOM && 
            sym != kBit0 && sym != kBit1) {
            userSymbols.push_back(sym);
        }
    }
    
    for (const auto& origSym : userSymbols) {
        StateId afterMarker = ctx.allocState();
        ctx.tt->add(entry, origSym, {afterMarker, kPosMarker, Move::Stay});
        
        StateId returnState = ctx.allocState();
        genReturnToMarker(ctx, returnState, exit, origSym);
        
        StateId afterWrite0 = ctx.allocState();
        
        StateId goToMem = afterMarker;
        StateId afterEOM = ctx.allocState();
        
        // Идём к EOM
        Move dirToMem = ctx.phaseR ? Move::Left : Move::Right;
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymEOM) {
                ctx.tt->add(goToMem, sym, {afterEOM, sym, Move::Stay});
            } else {
                ctx.tt->add(goToMem, sym, {goToMem, sym, dirToMem});
            }
        }
        
        StateId onLSB = ctx.allocState();
        genMoveLeftAll(ctx, afterEOM, onLSB);
        
        StateId checkBit = onLSB;
        Move carryDir = Move::Left;  // Carry на лево
        Symbol boundaryMarker = kSymBOM;
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kBit0) {
                // 0 + 1 = 1, без carry
                ctx.tt->add(checkBit, sym, {returnState, kBit1, Move::Stay});
            } else if (sym == kBit1) {
                // 1 + 1 = 0 + carry
                ctx.tt->add(checkBit, sym, {afterWrite0, kBit0, Move::Stay});
            } else if (sym == boundaryMarker) {
                // Overflow, stop
                ctx.tt->add(checkBit, sym, {returnState, sym, Move::Stay});
            } else {
                ctx.tt->add(checkBit, sym, {returnState, sym, Move::Stay});
            }
        }
        
        for (const auto& sym : ctx.alphabet) {
            ctx.tt->add(afterWrite0, sym, {checkBit, sym, carryDir});
        }
    }
    
    // Системные символы
    for (const auto& sym : ctx.alphabet) {
        if (sym == kPosMarker || sym == kSymBOM || sym == kSymEOM ||
            sym == kBit0 || sym == kBit1) {
            ctx.tt->add(entry, sym, {exit, sym, Move::Stay});
        }
    }
    
    return userSymbols.size() * 8;
}

StateId genDecInt8(CodegenContext& ctx, StateId entry, StateId exit) {
    // Декремент x--
    // То же что и ++, но наоборот
    
    std::vector<Symbol> userSymbols;
    for (const auto& sym : ctx.alphabet) {
        if (sym != kPosMarker && sym != kSymBOM && sym != kSymEOM && 
            sym != kBit0 && sym != kBit1) {
            userSymbols.push_back(sym);
        }
    }
    
    for (const auto& origSym : userSymbols) {
        StateId afterMarker = ctx.allocState();
        ctx.tt->add(entry, origSym, {afterMarker, kPosMarker, Move::Stay});
        
        StateId returnState = ctx.allocState();
        genReturnToMarker(ctx, returnState, exit, origSym);
        
        StateId afterWrite1 = ctx.allocState();
        
        StateId goToMem = afterMarker;
        StateId afterEOM = ctx.allocState();
        
        Move dirToMem = ctx.phaseR ? Move::Left : Move::Right;
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymEOM) {
                ctx.tt->add(goToMem, sym, {afterEOM, sym, Move::Stay});
            } else {
                ctx.tt->add(goToMem, sym, {goToMem, sym, dirToMem});
            }
        }
        
        StateId onLSB = ctx.allocState();
        genMoveLeftAll(ctx, afterEOM, onLSB);
        
        StateId checkBit = onLSB;
        Move borrowDir = Move::Left;
        Symbol boundaryMarker = kSymBOM;
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kBit1) {
                // 1 - 1 = 0, без необходимости старшего разряда
                ctx.tt->add(checkBit, sym, {returnState, kBit0, Move::Stay});
            } else if (sym == kBit0) {
                // 0 - 1 = 1 + нужен старший разряд
                ctx.tt->add(checkBit, sym, {afterWrite1, kBit1, Move::Stay});
            } else if (sym == boundaryMarker) {
                // Overflow, stop
                ctx.tt->add(checkBit, sym, {returnState, sym, Move::Stay});
            } else {
                ctx.tt->add(checkBit, sym, {returnState, sym, Move::Stay});
            }
        }
        
        for (const auto& sym : ctx.alphabet) {
            ctx.tt->add(afterWrite1, sym, {checkBit, sym, borrowDir});
        }
    }
    
    for (const auto& sym : ctx.alphabet) {
        if (sym == kPosMarker || sym == kSymBOM || sym == kSymEOM ||
            sym == kBit0 || sym == kBit1) {
            ctx.tt->add(entry, sym, {exit, sym, Move::Stay});
        }
    }
    
    return userSymbols.size() * 8;
}

StateId genCmpInt8Const_LT(CodegenContext& ctx, StateId entry, 
                           StateId ifTrue, StateId ifFalse, int rhs) {
    // Сравнение x < rhs
    
    Symbol rhsBits[8];
    int8ToBits(rhs, rhsBits);
    
    bool rhsNegative = (rhs < 0);
    
    std::vector<Symbol> userSymbols;
    for (const auto& sym : ctx.alphabet) {
        if (sym != kPosMarker && sym != kSymBOM && sym != kSymEOM && 
            sym != kBit0 && sym != kBit1) {
            userSymbols.push_back(sym);
        }
    }
    
    for (const auto& origSym : userSymbols) {
        StateId afterMarker = ctx.allocState();
        ctx.tt->add(entry, origSym, {afterMarker, kPosMarker, Move::Stay});
        
        StateId returnThenTrue = ctx.allocState();
        StateId returnThenFalse = ctx.allocState();
        genReturnToMarker(ctx, returnThenTrue, ifTrue, origSym);
        genReturnToMarker(ctx, returnThenFalse, ifFalse, origSym);
        
        StateId goToMem = afterMarker;
        StateId afterBOM = ctx.allocState();
        Move dirToMem = ctx.phaseR ? Move::Left : Move::Right;
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymBOM) {
                ctx.tt->add(goToMem, sym, {afterBOM, sym, Move::Stay});
            } else {
                ctx.tt->add(goToMem, sym, {goToMem, sym, dirToMem});
            }
        }
        
        StateId onMSB = ctx.allocState();
        genMoveRightAll(ctx, afterBOM, onMSB);
        Move nextBitDir = Move::Right;  // Сравниваем по битам, двигаясь в право
        
        StateId compareRest = ctx.allocState();
        
        // Проверка знакового бита
        for (const auto& sym : ctx.alphabet) {
            if (sym == kBit0) {
                if (rhsNegative) {
                    // x >= 0, rhs < 0 ==> FALSE
                    ctx.tt->add(onMSB, sym, {returnThenFalse, sym, Move::Stay});
                } else {
                    // x >= 0, rhs >= 0 ==> NEXT
                    ctx.tt->add(onMSB, sym, {compareRest, sym, nextBitDir});
                }
            } else if (sym == kBit1) {
                if (rhsNegative) {
                    // x < 0, rhs < 0 ==> NEXT
                    ctx.tt->add(onMSB, sym, {compareRest, sym, nextBitDir});
                } else {
                    // x < 0, rhs >= 0 ==> TRUE
                    ctx.tt->add(onMSB, sym, {returnThenTrue, sym, Move::Stay});
                }
            } else {
                ctx.tt->add(onMSB, sym, {returnThenFalse, sym, Move::Stay});
            }
        }
        
        StateId currentCompare = compareRest;
        
        for (int i = 1; i < kMemBits; i++) {
            StateId nextCompare = (i < kMemBits - 1) ? ctx.allocState() : StateId(-1);
            
            for (const auto& sym : ctx.alphabet) {
                if (sym == kBit0) {
                    if (rhsBits[i] == kBit0) {
                        if (i < kMemBits - 1) {
                            ctx.tt->add(currentCompare, sym, {nextCompare, sym, nextBitDir});
                        } else {
                            ctx.tt->add(currentCompare, sym, {returnThenFalse, sym, Move::Stay});
                        }
                    } else {
                        // x_bit = 0, rhs_bit = 1 ==> TRUE
                        ctx.tt->add(currentCompare, sym, {returnThenTrue, sym, Move::Stay});
                    }
                } else if (sym == kBit1) {
                    if (rhsBits[i] == kBit1) {
                        if (i < kMemBits - 1) {
                            ctx.tt->add(currentCompare, sym, {nextCompare, sym, nextBitDir});
                        } else {
                            ctx.tt->add(currentCompare, sym, {returnThenFalse, sym, Move::Stay});
                        }
                    } else {
                        // x_bit = 1, rhs_bit = 0 ==> FALSE
                        ctx.tt->add(currentCompare, sym, {returnThenFalse, sym, Move::Stay});
                    }
                } else {
                    ctx.tt->add(currentCompare, sym, {returnThenFalse, sym, Move::Stay});
                }
            }
            
            currentCompare = nextCompare;
        }
    }
    
    for (const auto& sym : ctx.alphabet) {
        if (sym == kPosMarker || sym == kSymBOM || sym == kSymEOM ||
            sym == kBit0 || sym == kBit1) {
            ctx.tt->add(entry, sym, {ifFalse, sym, Move::Stay});
        }
    }
    
    return 10 + kMemBits;
}

StateId genCmpInt8Const_GT(CodegenContext& ctx, StateId entry, 
                           StateId ifTrue, StateId ifFalse, int rhs) {
    // Сравнение x > rhs
    // Аналогично LT, но реверснуто

    Symbol rhsBits[8];
    int8ToBits(rhs, rhsBits);
    
    bool rhsNegative = (rhs < 0);
    
    std::vector<Symbol> userSymbols;
    for (const auto& sym : ctx.alphabet) {
        if (sym != kPosMarker && sym != kSymBOM && sym != kSymEOM && 
            sym != kBit0 && sym != kBit1) {
            userSymbols.push_back(sym);
        }
    }
    
    for (const auto& origSym : userSymbols) {
        StateId afterMarker = ctx.allocState();
        ctx.tt->add(entry, origSym, {afterMarker, kPosMarker, Move::Stay});
        
        StateId returnThenTrue = ctx.allocState();
        StateId returnThenFalse = ctx.allocState();
        genReturnToMarker(ctx, returnThenTrue, ifTrue, origSym);
        genReturnToMarker(ctx, returnThenFalse, ifFalse, origSym);
        
        StateId goToMem = afterMarker;
        StateId afterBOM = ctx.allocState();
        Move dirToMem = ctx.phaseR ? Move::Left : Move::Right;
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kSymBOM) {
                ctx.tt->add(goToMem, sym, {afterBOM, sym, Move::Stay});
            } else {
                ctx.tt->add(goToMem, sym, {goToMem, sym, dirToMem});
            }
        }
        
        StateId onMSB = ctx.allocState();
        genMoveRightAll(ctx, afterBOM, onMSB);
        Move nextBitDir = Move::Right;
        
        StateId compareRest = ctx.allocState();
        
        for (const auto& sym : ctx.alphabet) {
            if (sym == kBit0) {
                if (rhsNegative) {
                    ctx.tt->add(onMSB, sym, {returnThenTrue, sym, Move::Stay});
                } else {
                    ctx.tt->add(onMSB, sym, {compareRest, sym, nextBitDir});
                }
            } else if (sym == kBit1) {
                if (rhsNegative) {
                    ctx.tt->add(onMSB, sym, {compareRest, sym, nextBitDir});
                } else {
                    ctx.tt->add(onMSB, sym, {returnThenFalse, sym, Move::Stay});
                }
            } else {
                ctx.tt->add(onMSB, sym, {returnThenFalse, sym, Move::Stay});
            }
        }
        
        StateId currentCompare = compareRest;
        
        for (int i = 1; i < kMemBits; i++) {
            StateId nextCompare = (i < kMemBits - 1) ? ctx.allocState() : StateId(-1);
            
            for (const auto& sym : ctx.alphabet) {
                if (sym == kBit0) {
                    if (rhsBits[i] == kBit0) {
                        if (i < kMemBits - 1) {
                            ctx.tt->add(currentCompare, sym, {nextCompare, sym, nextBitDir});
                        } else {
                            ctx.tt->add(currentCompare, sym, {returnThenFalse, sym, Move::Stay});
                        }
                    } else {
                        ctx.tt->add(currentCompare, sym, {returnThenFalse, sym, Move::Stay});
                    }
                } else if (sym == kBit1) {
                    if (rhsBits[i] == kBit1) {
                        if (i < kMemBits - 1) {
                            ctx.tt->add(currentCompare, sym, {nextCompare, sym, nextBitDir});
                        } else {
                            ctx.tt->add(currentCompare, sym, {returnThenFalse, sym, Move::Stay});
                        }
                    } else {
                        ctx.tt->add(currentCompare, sym, {returnThenTrue, sym, Move::Stay});
                    }
                } else {
                    ctx.tt->add(currentCompare, sym, {returnThenFalse, sym, Move::Stay});
                }
            }
            
            currentCompare = nextCompare;
        }
    }
    
    for (const auto& sym : ctx.alphabet) {
        if (sym == kPosMarker || sym == kSymBOM || sym == kSymEOM ||
            sym == kBit0 || sym == kBit1) {
            ctx.tt->add(entry, sym, {ifFalse, sym, Move::Stay});
        }
    }
    
    return 10 + kMemBits;
}



// Подсчёт состояний

// Вспомогательная функция для подсчёта пользовательских символов
static size_t countUserSymbols(const std::vector<Symbol>& alphabet) {
    size_t count = 0;
    for (const auto& sym : alphabet) {
        if (sym != kPosMarker && sym != kSymBOM && sym != kSymEOM && 
            sym != kBit0 && sym != kBit1) {
            count++;
        }
    }
    return count;
}

StateId countVarSetConstStates(const std::vector<Symbol>& alphabet) {
    // Для каждого пользовательского символа делаем полную цепочку - идея с #.
    // Лучше перебрать с запасом, чем недобрать.
    size_t userSyms = countUserSymbols(alphabet);
    // afterMarker + genGoToBOM(1) + 8*(onBit + afterWrite) + genReturnToMarker (2)
    // ИТОГО: 1 + 1 + 16 + 2 = 20 на символ, берём 30 для запаса - некоторые просто не юзаем
    return static_cast<StateId>(userSyms * 30);
}

StateId countVarIncStates(const std::vector<Symbol>& alphabet) {
    size_t userSyms = countUserSymbols(alphabet);
    // afterMarker + genGoToEOM(1) + onLSB + returnState + afterWrite0 + genReturnToMarker(2)
    // ИТОГО: 7, берём 15 для запаса
    return static_cast<StateId>(userSyms * 15);
}

StateId countVarDecStates(const std::vector<Symbol>& alphabet) {
    size_t userSyms = countUserSymbols(alphabet);
    return static_cast<StateId>(userSyms * 15);
}

StateId countCmpInt8States(const std::vector<Symbol>& alphabet, int /*rhs*/) {
    size_t userSyms = countUserSymbols(alphabet);
    // afterMarker + genGoToBOM(1) + onMSB + returnThenTrue + returnThenFalse 
    // + compareRest + 6 nextCompare + 2*genReturnToMarker(2)
    // ИТОГО: 16, берём 25 для запаса
    return static_cast<StateId>(userSyms * 25);
}
