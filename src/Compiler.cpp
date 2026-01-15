#include "Compiler.h"
#include "Condition.h"
#include "Flatten.h"
#include "IR.h"
#include "Lexer.h"
#include "TransitionGenerator.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

static std::vector<std::string> splitBySpaces(const std::string& str) {
    std::vector<std::string> tokens;
    std::istringstream iss(str);
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

// flatten and transition generation moved to dedicated modules

CompileResult Compiler::compile(std::string_view source) const {
    CompileResult result;
    result.ok = true;

    const Symbol blankSymbol = " ";
    std::unordered_set<Symbol> alphabetSet;
    alphabetSet.insert(blankSymbol);
    result.alphabet.push_back(blankSymbol);

    bool alphabetDefined = false;
    bool setupDefined = false;

    std::unordered_map<std::string, Procedure> procedures;
    Procedure* currentProc = nullptr;

    Lexer lexer(source);
    Token token = lexer.next();

    auto error = [&](int line, int col, const std::string& msg) {
        result.ok = false;
        result.diagnostics.push_back({DiagnosticLevel::Error, line, col, msg});
    };

    auto expect = [&](TokenType expected, const std::string& what) -> bool {
        if (token.type != expected) {
            error(token.line, token.column, "Ожидался " + what);
            return false;
        }
        return true;
    };

    auto addInstruction = [&](std::shared_ptr<IRInstruction> instr) {
        if (!currentProc) {
            error(instr->line, instr->column, "Инструкция вне процедуры");
            return false;
        }
        currentProc->body.push_back(instr);
        return true;
    };

    while (token.type != TokenType::Eof && result.ok) {
        if (token.type == TokenType::Identifier) {
            const std::string& cmd = token.value;
            const int cmdLine = token.line;
            const int cmdCol = token.column;

            if (cmd == "Set_alphabet") {
                if (currentProc) {
                    error(cmdLine, cmdCol, "Set_alphabet не может быть внутри процедуры");
                    break;
                }
                if (setupDefined) {
                    error(cmdLine, cmdCol, "Set_alphabet должен быть перед Setup");
                    break;
                }
                if (alphabetDefined) {
                    error(cmdLine, cmdCol, "Set_alphabet уже был определён (повторный вызов запрещён)");
                    break;
                }
                if (!procedures.empty()) {
                    error(cmdLine, cmdCol, "Set_alphabet должен быть перед определением процедур");
                    break;
                }

                // Парсим аргумент - строку с символами алфавита
                token = lexer.next();
                if (!expect(TokenType::StringLiteral, "строка с алфавитом")) break;

                const std::string content = token.value;
                const int strLine = token.line;
                const int strCol = token.column;

                token = lexer.next();
                if (!expect(TokenType::Semicolon, ";")) break;

                // Разбиваем строку по пробелам и добавляем символы в алфавит
                auto symbols = splitBySpaces(content);
                for (const auto& sym : symbols) {
                    // "blank" - зарезервированное слово
                    if (sym == "blank") {
                        error(strLine, strCol, "Имя 'blank' зарезервировано и не может использоваться в алфавите");
                        break;
                    }
                    // Проверка на дубликаты
                    if (alphabetSet.count(sym)) {
                        error(strLine, strCol, "Дублирующийся символ в алфавите: '" + sym + "'");
                        break;
                    }
                    alphabetSet.insert(sym);
                    result.alphabet.push_back(sym);
                }

                alphabetDefined = true;
                token = lexer.next();

            // Setup "содержимое ленты"; - начальное содержимое ленты
            } else if (cmd == "Setup") {
                if (currentProc) {
                    error(cmdLine, cmdCol, "Setup не может быть внутри процедуры");
                    break;
                }
                if (!alphabetDefined) {
                    error(cmdLine, cmdCol, "Setup должен быть после Set_alphabet");
                    break;
                }
                if (setupDefined) {
                    error(cmdLine, cmdCol, "Setup уже был определён (повторный вызов запрещён)");
                    break;
                }
                if (!procedures.empty()) {
                    error(cmdLine, cmdCol, "Setup должен быть перед определением процедур");
                    break;
                }

                token = lexer.next();
                if (!expect(TokenType::StringLiteral, "строка с начальным содержимым ленты")) break;

                const std::string content = token.value;
                const int strLine = token.line;
                const int strCol = token.column;

                token = lexer.next();
                if (!expect(TokenType::Semicolon, ";")) break;

                auto symbols = splitBySpaces(content);
                long long pos = 0;
                for (const auto& sym : symbols) {
                    Symbol actualSym = (sym == "blank") ? blankSymbol : sym;
                    if (actualSym != blankSymbol && !alphabetSet.count(actualSym)) {
                        error(strLine, strCol, "Символ '" + sym + "' не определён в алфавите");
                        break;
                    }
                    result.initialTape.set(pos, actualSym);
                    pos++;
                }

                setupDefined = true;
                token = lexer.next();

            // proc имя() { ... } - определение процедуры
            } else if (cmd == "proc") {
                // Проверка: нельзя определять процедуру внутри другой
                if (currentProc) {
                    error(cmdLine, cmdCol, "Вложенные процедуры не поддерживаются");
                    break;
                }
                // Проверка: алфавит должен быть уже определён
                if (!alphabetDefined) {
                    error(cmdLine, cmdCol, "proc: сначала нужно определить Set_alphabet");
                    break;
                }

                // Парсим имя процедуры
                token = lexer.next();
                if (!expect(TokenType::Identifier, "имя процедуры")) break;

                const std::string procName = token.value;
                const int nameL = token.line;
                const int nameC = token.column;

                // Проверка на дубликаты имён
                if (procedures.count(procName)) {
                    error(nameL, nameC, "Процедура '" + procName + "' уже определена");
                    break;
                }

                // Парсим синтаксис: ()  {
                token = lexer.next();
                if (!expect(TokenType::LParen, "(")) break;
                token = lexer.next();
                if (!expect(TokenType::RParen, ")")) break;
                token = lexer.next();
                if (!expect(TokenType::LBrace, "{")) break;

                // Создаём новую процедуру и устанавливаем её как текущую
                Procedure proc;
                proc.name = procName;
                proc.line = cmdLine;
                proc.column = cmdCol;

                procedures[procName] = proc;
                currentProc = &procedures[procName];
                token = lexer.next();

            // move_left; - перемещение головки влево
            } else if (cmd == "move_left") {
                if (!alphabetDefined) {
                    error(cmdLine, cmdCol, "move_left: сначала нужно определить Set_alphabet");
                    break;
                }

                token = lexer.next();
                if (!expect(TokenType::Semicolon, ";")) break;

                if (!addInstruction(IRInstruction::simple(IRType::MoveLeft, "", cmdLine, cmdCol))) break;
                token = lexer.next();

            // move_right; - перемещение головки вправо
            } else if (cmd == "move_right") {
                if (!alphabetDefined) {
                    error(cmdLine, cmdCol, "move_right: сначала нужно определить Set_alphabet");
                    break;
                }

                token = lexer.next();
                if (!expect(TokenType::Semicolon, ";")) break;

                if (!addInstruction(IRInstruction::simple(IRType::MoveRight, "", cmdLine, cmdCol))) break;
                token = lexer.next();

            // write "символ"; - запись символа на ленту
            } else if (cmd == "write") {
                if (!alphabetDefined) {
                    error(cmdLine, cmdCol, "write: сначала нужно определить Set_alphabet");
                    break;
                }

                token = lexer.next();
                if (!expect(TokenType::StringLiteral, "символ для записи")) break;

                const std::string symStr = token.value;
                const int strLine = token.line;
                const int strCol = token.column;

                Symbol actualSym = (symStr == "blank") ? blankSymbol : symStr;
                if (actualSym != blankSymbol && !alphabetSet.count(actualSym)) {
                    error(strLine, strCol, "Символ '" + symStr + "' не определён в алфавите");
                    break;
                }

                token = lexer.next();
                if (!expect(TokenType::Semicolon, ";")) break;

                if (!addInstruction(IRInstruction::simple(IRType::Write, actualSym, cmdLine, cmdCol))) break;
                token = lexer.next();

            // call имя; - вызов процедуры
            } else if (cmd == "call") {
                if (!alphabetDefined) {
                    error(cmdLine, cmdCol, "call: сначала нужно определить Set_alphabet");
                    break;
                }

                token = lexer.next();
                if (!expect(TokenType::Identifier, "имя процедуры")) break;

                const std::string procName = token.value;
                const int nameL = token.line;
                const int nameC = token.column;

                if (!procedures.count(procName)) {
                    error(nameL, nameC, "Процедура '" + procName + "' не определена");
                    break;
                }

                token = lexer.next();
                if (!expect(TokenType::Semicolon, ";")) break;

                if (!addInstruction(IRInstruction::simple(IRType::Call, procName, cmdLine, cmdCol))) break;
                token = lexer.next();

            // if (условие) { ... } [else { ... }] - условная конструкция
            } else if (cmd == "if") {
                if (!alphabetDefined) {
                    error(cmdLine, cmdCol, "if: сначала нужно определить Set_alphabet");
                    break;
                }

                // Ожидаем: ( условие ) { тело }
                token = lexer.next();
                if (!expect(TokenType::LParen, "(")) break;
                token = lexer.next();

                // Парсим условие с помощью ConditionParser
                ConditionParser condParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                ConditionPtr cond = condParser.parse();
                if (!cond || !result.ok) break;

                if (!expect(TokenType::RParen, ")")) break;
                token = lexer.next();

                if (!expect(TokenType::LBrace, "{")) break;
                token = lexer.next();

                // Парсинг вложенных блоков
                IRBlock thenBranch;
                std::vector<IRBlock*> blockStack;                           // стек родительских блоков
                std::vector<std::shared_ptr<IRInstruction>> pendingIfStack; // стек if-ов, ожидающих else
                
                IRBlock* currentBlock = &thenBranch;                        // текущий блок для добавления инструкций
                
                // Парсим содержимое блока
                while (result.ok && token.type != TokenType::Eof) {
                    // Закрывающая скобка - конец текущего блока
                    if (token.type == TokenType::RBrace) {
                        if (!pendingIfStack.empty()) {
                            // Завершаем внутренний if/else блок
                            auto pendingIf = pendingIfStack.back();
                            pendingIfStack.pop_back();
                            
                            if (!blockStack.empty()) {
                                currentBlock = blockStack.back();
                                blockStack.pop_back();
                            }
                            
                            // Проверяем, есть ли else или else if после }
                            token = lexer.next();
                            if (token.type == TokenType::Identifier && token.value == "else") {
                                token = lexer.next();
                                if (token.type == TokenType::Identifier && token.value == "if") {
                                    // else if - создаём вложенный if в else-ветке
                                    int elseIfLine = token.line;
                                    int elseIfCol = token.column;
                                    token = lexer.next();
                                    if (!expect(TokenType::LParen, "(")) break;
                                    token = lexer.next();

                                    // Парсим условие else if
                                    ConditionParser elseIfCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                                    ConditionPtr elseIfCond = elseIfCondParser.parse();
                                    if (!elseIfCond || !result.ok) break;

                                    if (!expect(TokenType::RParen, ")")) break;
                                    token = lexer.next();
                                    if (!expect(TokenType::LBrace, "{")) break;
                                    token = lexer.next();

                                    // Создаём вложенный if внутри else-ветки
                                    auto nestedElseIf = IRInstruction::ifElse(elseIfCond, IRBlock{}, IRBlock{}, elseIfLine, elseIfCol);
                                    pendingIf->elseBranch.push_back(nestedElseIf);
                                    blockStack.push_back(currentBlock);
                                    pendingIfStack.push_back(nestedElseIf);
                                    
                                    // Сохраняем pendingIf для возможного else
                                    blockStack.push_back(&pendingIf->elseBranch);
                                    pendingIfStack.push_back(pendingIf);
                                    currentBlock = &nestedElseIf->thenBranch;
                                } else if (token.type == TokenType::LBrace) {
                                    // else { ... } - парсим else-ветку
                                    token = lexer.next();
                                    blockStack.push_back(currentBlock);
                                    pendingIfStack.push_back(pendingIf);
                                    currentBlock = &pendingIf->elseBranch;
                                } else {
                                    error(token.line, token.column, "После 'else' ожидалась '{' или 'if'");
                                    break;
                                }
                            } else {
                                // Нет else - добавляем if в текущий блок
                                currentBlock->push_back(pendingIf);
                            }
                        } else {
                            // Конец главного if-блока
                            break;
                        }
                    } else if (token.type == TokenType::Identifier) {
                        // Парсим инструкции внутри блока
                        const std::string innerCmd = token.value;
                        const int innerLine = token.line;
                        const int innerCol = token.column;

                        if (innerCmd == "move_left") {
                            token = lexer.next();
                            if (!expect(TokenType::Semicolon, ";")) break;
                            currentBlock->push_back(IRInstruction::simple(IRType::MoveLeft, "", innerLine, innerCol));
                            token = lexer.next();
                        } else if (innerCmd == "move_right") {
                            token = lexer.next();
                            if (!expect(TokenType::Semicolon, ";")) break;
                            currentBlock->push_back(IRInstruction::simple(IRType::MoveRight, "", innerLine, innerCol));
                            token = lexer.next();
                        } else if (innerCmd == "write") {
                            token = lexer.next();
                            if (!expect(TokenType::StringLiteral, "символ для записи")) break;
                            const std::string symStr = token.value;
                            Symbol actualSym = (symStr == "blank") ? blankSymbol : symStr;
                            if (actualSym != blankSymbol && !alphabetSet.count(actualSym)) {
                                error(token.line, token.column, "Символ '" + symStr + "' не определён в алфавите");
                                break;
                            }
                            token = lexer.next();
                            if (!expect(TokenType::Semicolon, ";")) break;
                            currentBlock->push_back(IRInstruction::simple(IRType::Write, actualSym, innerLine, innerCol));
                            token = lexer.next();
                        } else if (innerCmd == "call") {
                            token = lexer.next();
                            if (!expect(TokenType::Identifier, "имя процедуры")) break;
                            const std::string pName = token.value;
                            if (!procedures.count(pName)) {
                                error(token.line, token.column, "Процедура '" + pName + "' не определена");
                                break;
                            }
                            token = lexer.next();
                            if (!expect(TokenType::Semicolon, ";")) break;
                            currentBlock->push_back(IRInstruction::simple(IRType::Call, pName, innerLine, innerCol));
                            token = lexer.next();
                        } else if (innerCmd == "if") {
                            // Вложенный if
                            token = lexer.next();
                            if (!expect(TokenType::LParen, "(")) break;
                            token = lexer.next();

                            ConditionParser innerCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                            ConditionPtr innerCond = innerCondParser.parse();
                            if (!innerCond || !result.ok) break;

                            if (!expect(TokenType::RParen, ")")) break;
                            token = lexer.next();
                            if (!expect(TokenType::LBrace, "{")) break;
                            token = lexer.next();

                            auto nestedIf = IRInstruction::ifElse(innerCond, IRBlock{}, IRBlock{}, innerLine, innerCol);
                            blockStack.push_back(currentBlock);
                            pendingIfStack.push_back(nestedIf);
                            currentBlock = &nestedIf->thenBranch;
                        } else if (innerCmd == "while") {
                            // Вложенный while
                            token = lexer.next();
                            if (!expect(TokenType::LParen, "(")) break;
                            token = lexer.next();

                            ConditionParser innerCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                            ConditionPtr innerCond = innerCondParser.parse();
                            if (!innerCond || !result.ok) break;

                            if (!expect(TokenType::RParen, ")")) break;
                            token = lexer.next();
                            if (!expect(TokenType::LBrace, "{")) break;
                            token = lexer.next();

                            auto nestedWhile = IRInstruction::whileLoop(innerCond, IRBlock{}, innerLine, innerCol);
                            blockStack.push_back(currentBlock);
                            pendingIfStack.push_back(nestedWhile);
                            currentBlock = &nestedWhile->thenBranch;
                        } else {
                            error(innerLine, innerCol, "Неизвестная команда внутри if: '" + innerCmd + "'");
                            break;
                        }
                    } else {
                        error(token.line, token.column, "Ожидалась команда или '}'");
                        break;
                    }
                }

                if (!result.ok) break;

                // Теперь проверяем наличие else или else if после основного блока if
                token = lexer.next();
                IRBlock elseBranch;
                
                if (token.type == TokenType::Identifier && token.value == "else") {
                    token = lexer.next();
                    
                    if (token.type == TokenType::Identifier && token.value == "if") {
                        // else if - парсим как вложенный if в else ветке
                        int elseIfLine = token.line;
                        int elseIfCol = token.column;
                        token = lexer.next();
                        if (!expect(TokenType::LParen, "(")) break;
                        token = lexer.next();

                        ConditionParser elseIfCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                        ConditionPtr elseIfCond = elseIfCondParser.parse();
                        if (!elseIfCond || !result.ok) break;

                        if (!expect(TokenType::RParen, ")")) break;
                        token = lexer.next();
                        if (!expect(TokenType::LBrace, "{")) break;
                        token = lexer.next();

                        auto nestedElseIf = IRInstruction::ifElse(elseIfCond, IRBlock{}, IRBlock{}, elseIfLine, elseIfCol);
                        
                        // Парсим тело else-if и возможную цепочку
                        currentBlock = &nestedElseIf->thenBranch;
                        pendingIfStack.clear();
                        blockStack.clear();
                        pendingIfStack.push_back(nestedElseIf);
                        
                        while (result.ok && token.type != TokenType::Eof) {
                            if (token.type == TokenType::RBrace) {
                                if (pendingIfStack.size() > 1) {
                                    auto innerPending = pendingIfStack.back();
                                    pendingIfStack.pop_back();
                                    
                                    if (!blockStack.empty()) {
                                        currentBlock = blockStack.back();
                                        blockStack.pop_back();
                                    }
                                    
                                    token = lexer.next();
                                    if (token.type == TokenType::Identifier && token.value == "else") {
                                        token = lexer.next();
                                        if (token.type == TokenType::Identifier && token.value == "if") {
                                            // Очередной else if в цепочке
                                            int chainLine = token.line;
                                            int chainCol = token.column;
                                            token = lexer.next();
                                            if (!expect(TokenType::LParen, "(")) break;
                                            token = lexer.next();

                                            ConditionParser chainCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                                            ConditionPtr chainCond = chainCondParser.parse();
                                            if (!chainCond || !result.ok) break;

                                            if (!expect(TokenType::RParen, ")")) break;
                                            token = lexer.next();
                                            if (!expect(TokenType::LBrace, "{")) break;
                                            token = lexer.next();

                                            auto chainIf = IRInstruction::ifElse(chainCond, IRBlock{}, IRBlock{}, chainLine, chainCol);
                                            innerPending->elseBranch.push_back(chainIf);
                                            blockStack.push_back(currentBlock);
                                            pendingIfStack.push_back(chainIf);
                                            blockStack.push_back(&innerPending->elseBranch);
                                            pendingIfStack.push_back(innerPending);
                                            currentBlock = &chainIf->thenBranch;
                                        } else if (token.type == TokenType::LBrace) {
                                            token = lexer.next();
                                            blockStack.push_back(currentBlock);
                                            pendingIfStack.push_back(innerPending);
                                            currentBlock = &innerPending->elseBranch;
                                        } else {
                                            error(token.line, token.column, "После 'else' ожидалась '{' или 'if'");
                                            break;
                                        }
                                    } else {
                                        currentBlock->push_back(innerPending);
                                    }
                                } else {
                                    // Конец блока else-if
                                    break;
                                }
                            } else if (token.type == TokenType::Identifier) {
                                const std::string innerCmd = token.value;
                                const int innerLine = token.line;
                                const int innerCol = token.column;

                                if (innerCmd == "move_left") {
                                    token = lexer.next();
                                    if (!expect(TokenType::Semicolon, ";")) break;
                                    currentBlock->push_back(IRInstruction::simple(IRType::MoveLeft, "", innerLine, innerCol));
                                    token = lexer.next();
                                } else if (innerCmd == "move_right") {
                                    token = lexer.next();
                                    if (!expect(TokenType::Semicolon, ";")) break;
                                    currentBlock->push_back(IRInstruction::simple(IRType::MoveRight, "", innerLine, innerCol));
                                    token = lexer.next();
                                } else if (innerCmd == "write") {
                                    token = lexer.next();
                                    if (!expect(TokenType::StringLiteral, "символ для записи")) break;
                                    const std::string symStr = token.value;
                                    Symbol actualSym = (symStr == "blank") ? blankSymbol : symStr;
                                    if (actualSym != blankSymbol && !alphabetSet.count(actualSym)) {
                                        error(token.line, token.column, "Символ '" + symStr + "' не определён в алфавите");
                                        break;
                                    }
                                    token = lexer.next();
                                    if (!expect(TokenType::Semicolon, ";")) break;
                                    currentBlock->push_back(IRInstruction::simple(IRType::Write, actualSym, innerLine, innerCol));
                                    token = lexer.next();
                                } else if (innerCmd == "call") {
                                    token = lexer.next();
                                    if (!expect(TokenType::Identifier, "имя процедуры")) break;
                                    const std::string pName = token.value;
                                    if (!procedures.count(pName)) {
                                        error(token.line, token.column, "Процедура '" + pName + "' не определена");
                                        break;
                                    }
                                    token = lexer.next();
                                    if (!expect(TokenType::Semicolon, ";")) break;
                                    currentBlock->push_back(IRInstruction::simple(IRType::Call, pName, innerLine, innerCol));
                                    token = lexer.next();
                                } else if (innerCmd == "if" || innerCmd == "while") {
                                    bool isWhile = (innerCmd == "while");
                                    token = lexer.next();
                                    if (!expect(TokenType::LParen, "(")) break;
                                    token = lexer.next();

                                    ConditionParser innerCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                                    ConditionPtr innerCond = innerCondParser.parse();
                                    if (!innerCond || !result.ok) break;

                                    if (!expect(TokenType::RParen, ")")) break;
                                    token = lexer.next();
                                    if (!expect(TokenType::LBrace, "{")) break;
                                    token = lexer.next();

                                    auto nested = isWhile 
                                        ? IRInstruction::whileLoop(innerCond, IRBlock{}, innerLine, innerCol)
                                        : IRInstruction::ifElse(innerCond, IRBlock{}, IRBlock{}, innerLine, innerCol);
                                    blockStack.push_back(currentBlock);
                                    pendingIfStack.push_back(nested);
                                    currentBlock = &nested->thenBranch;
                                } else {
                                    error(innerLine, innerCol, "Неизвестная команда внутри else if: '" + innerCmd + "'");
                                    break;
                                }
                            } else {
                                error(token.line, token.column, "Ожидалась команда или '}'");
                                break;
                            }
                        }
                        
                        if (!result.ok) break;
                        
                        // Проверяем наличие else после цепочки else-if
                        token = lexer.next();
                        if (token.type == TokenType::Identifier && token.value == "else") {
                            token = lexer.next();
                            if (token.type == TokenType::LBrace) {
                                // Финальный else
                                token = lexer.next();
                                currentBlock = &nestedElseIf->elseBranch;
                                pendingIfStack.clear();
                                blockStack.clear();
                                
                                while (result.ok && token.type != TokenType::Eof) {
                                    if (token.type == TokenType::RBrace) {
                                        if (!pendingIfStack.empty()) {
                                            auto pend = pendingIfStack.back();
                                            pendingIfStack.pop_back();
                                            if (!blockStack.empty()) {
                                                currentBlock = blockStack.back();
                                                blockStack.pop_back();
                                            }
                                            token = lexer.next();
                                            if (token.type == TokenType::Identifier && token.value == "else") {
                                                token = lexer.next();
                                                if (token.type == TokenType::LBrace) {
                                                    token = lexer.next();
                                                    blockStack.push_back(currentBlock);
                                                    pendingIfStack.push_back(pend);
                                                    currentBlock = &pend->elseBranch;
                                                } else {
                                                    error(token.line, token.column, "После 'else' ожидалась '{'");
                                                    break;
                                                }
                                            } else {
                                                currentBlock->push_back(pend);
                                            }
                                        } else {
                                            break;
                                        }
                                    } else if (token.type == TokenType::Identifier) {
                                        const std::string ic = token.value;
                                        const int il = token.line;
                                        const int icol = token.column;

                                        if (ic == "move_left") {
                                            token = lexer.next();
                                            if (!expect(TokenType::Semicolon, ";")) break;
                                            currentBlock->push_back(IRInstruction::simple(IRType::MoveLeft, "", il, icol));
                                            token = lexer.next();
                                        } else if (ic == "move_right") {
                                            token = lexer.next();
                                            if (!expect(TokenType::Semicolon, ";")) break;
                                            currentBlock->push_back(IRInstruction::simple(IRType::MoveRight, "", il, icol));
                                            token = lexer.next();
                                        } else if (ic == "write") {
                                            token = lexer.next();
                                            if (!expect(TokenType::StringLiteral, "символ")) break;
                                            Symbol s = (token.value == "blank") ? blankSymbol : token.value;
                                            if (s != blankSymbol && !alphabetSet.count(s)) {
                                                error(token.line, token.column, "Символ не в алфавите");
                                                break;
                                            }
                                            token = lexer.next();
                                            if (!expect(TokenType::Semicolon, ";")) break;
                                            currentBlock->push_back(IRInstruction::simple(IRType::Write, s, il, icol));
                                            token = lexer.next();
                                        } else if (ic == "call") {
                                            token = lexer.next();
                                            if (!expect(TokenType::Identifier, "имя")) break;
                                            if (!procedures.count(token.value)) {
                                                error(token.line, token.column, "Процедура не найдена");
                                                break;
                                            }
                                            std::string pn = token.value;
                                            token = lexer.next();
                                            if (!expect(TokenType::Semicolon, ";")) break;
                                            currentBlock->push_back(IRInstruction::simple(IRType::Call, pn, il, icol));
                                            token = lexer.next();
                                        } else if (ic == "if" || ic == "while") {
                                            bool isW = (ic == "while");
                                            token = lexer.next();
                                            if (!expect(TokenType::LParen, "(")) break;
                                            token = lexer.next();
                                            ConditionParser cp(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                                            auto cd = cp.parse();
                                            if (!cd || !result.ok) break;
                                            if (!expect(TokenType::RParen, ")")) break;
                                            token = lexer.next();
                                            if (!expect(TokenType::LBrace, "{")) break;
                                            token = lexer.next();
                                            auto n = isW ? IRInstruction::whileLoop(cd, IRBlock{}, il, icol)
                                                         : IRInstruction::ifElse(cd, IRBlock{}, IRBlock{}, il, icol);
                                            blockStack.push_back(currentBlock);
                                            pendingIfStack.push_back(n);
                                            currentBlock = &n->thenBranch;
                                        } else {
                                            error(il, icol, "Неизвестная команда");
                                            break;
                                        }
                                    } else {
                                        error(token.line, token.column, "Ожидалась команда");
                                        break;
                                    }
                                }
                                if (!result.ok) break;
                                token = lexer.next();
                            } else {
                                error(token.line, token.column, "После 'else' ожидалась '{'");
                                break;
                            }
                        }
                        
                        elseBranch.push_back(nestedElseIf);
                    } else if (token.type == TokenType::LBrace) {
                        token = lexer.next();

                        // Парсим ветку else (та же логика)
                        currentBlock = &elseBranch;
                        pendingIfStack.clear();
                        blockStack.clear();
                        
                        while (result.ok && token.type != TokenType::Eof) {
                            if (token.type == TokenType::RBrace) {
                                if (!pendingIfStack.empty()) {
                                    auto pendingIf = pendingIfStack.back();
                                    pendingIfStack.pop_back();
                                    
                                    if (!blockStack.empty()) {
                                        currentBlock = blockStack.back();
                                        blockStack.pop_back();
                                    }
                                    
                                    token = lexer.next();
                                    if (token.type == TokenType::Identifier && token.value == "else") {
                                        token = lexer.next();
                                        if (!expect(TokenType::LBrace, "{")) break;
                                        token = lexer.next();
                                        
                                        blockStack.push_back(currentBlock);
                                        pendingIfStack.push_back(pendingIf);
                                        currentBlock = &pendingIf->elseBranch;
                                    } else {
                                        currentBlock->push_back(pendingIf);
                                    }
                                } else {
                                    break;
                                }
                            } else if (token.type == TokenType::Identifier) {
                                const std::string innerCmd = token.value;
                                const int innerLine = token.line;
                                const int innerCol = token.column;

                                if (innerCmd == "move_left") {
                                    token = lexer.next();
                                    if (!expect(TokenType::Semicolon, ";")) break;
                                    currentBlock->push_back(IRInstruction::simple(IRType::MoveLeft, "", innerLine, innerCol));
                                    token = lexer.next();
                                } else if (innerCmd == "move_right") {
                                    token = lexer.next();
                                    if (!expect(TokenType::Semicolon, ";")) break;
                                    currentBlock->push_back(IRInstruction::simple(IRType::MoveRight, "", innerLine, innerCol));
                                    token = lexer.next();
                                } else if (innerCmd == "write") {
                                    token = lexer.next();
                                    if (!expect(TokenType::StringLiteral, "символ для записи")) break;
                                    const std::string symStr = token.value;
                                    Symbol actualSym = (symStr == "blank") ? blankSymbol : symStr;
                                    if (actualSym != blankSymbol && !alphabetSet.count(actualSym)) {
                                        error(token.line, token.column, "Символ '" + symStr + "' не определён в алфавите");
                                        break;
                                    }
                                    token = lexer.next();
                                    if (!expect(TokenType::Semicolon, ";")) break;
                                    currentBlock->push_back(IRInstruction::simple(IRType::Write, actualSym, innerLine, innerCol));
                                    token = lexer.next();
                                } else if (innerCmd == "call") {
                                    token = lexer.next();
                                    if (!expect(TokenType::Identifier, "имя процедуры")) break;
                                    const std::string pName = token.value;
                                    if (!procedures.count(pName)) {
                                        error(token.line, token.column, "Процедура '" + pName + "' не определена");
                                        break;
                                    }
                                    token = lexer.next();
                                    if (!expect(TokenType::Semicolon, ";")) break;
                                    currentBlock->push_back(IRInstruction::simple(IRType::Call, pName, innerLine, innerCol));
                                    token = lexer.next();
                                } else if (innerCmd == "if") {
                                    token = lexer.next();
                                    if (!expect(TokenType::LParen, "(")) break;
                                    token = lexer.next();

                                    ConditionParser innerCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                                    ConditionPtr innerCond = innerCondParser.parse();
                                    if (!innerCond || !result.ok) break;

                                    if (!expect(TokenType::RParen, ")")) break;
                                    token = lexer.next();
                                    if (!expect(TokenType::LBrace, "{")) break;
                                    token = lexer.next();

                                    auto nestedIf = IRInstruction::ifElse(innerCond, IRBlock{}, IRBlock{}, innerLine, innerCol);
                                    blockStack.push_back(currentBlock);
                                    pendingIfStack.push_back(nestedIf);
                                    currentBlock = &nestedIf->thenBranch;
                                } else if (innerCmd == "while") {
                                    token = lexer.next();
                                    if (!expect(TokenType::LParen, "(")) break;
                                    token = lexer.next();

                                    ConditionParser innerCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                                    ConditionPtr innerCond = innerCondParser.parse();
                                    if (!innerCond || !result.ok) break;

                                    if (!expect(TokenType::RParen, ")")) break;
                                    token = lexer.next();
                                    if (!expect(TokenType::LBrace, "{")) break;
                                    token = lexer.next();

                                    auto nestedWhile = IRInstruction::whileLoop(innerCond, IRBlock{}, innerLine, innerCol);
                                    blockStack.push_back(currentBlock);
                                    pendingIfStack.push_back(nestedWhile);
                                    currentBlock = &nestedWhile->thenBranch;
                                } else {
                                    error(innerLine, innerCol, "Неизвестная команда внутри else: '" + innerCmd + "'");
                                    break;
                                }
                            } else {
                                error(token.line, token.column, "Ожидалась команда или '}'");
                                break;
                            }
                        }
                        
                        if (!result.ok) break;
                        token = lexer.next();
                    } else {
                        error(token.line, token.column, "После 'else' ожидалась '{' или 'if'");
                        break;
                    }
                }

                // Создаём итоговую IR-инструкцию if/else с обеими ветками
                auto ifInstr = IRInstruction::ifElse(cond, std::move(thenBranch), std::move(elseBranch), cmdLine, cmdCol);
                if (!addInstruction(ifInstr)) break;

            // Цикл: while (условие) { ... }
            } else if (cmd == "while") {
                if (!alphabetDefined) {
                    error(cmdLine, cmdCol, "while: сначала нужно определить Set_alphabet");
                    break;
                }

                // Ожидаем: ( условие ) { тело }
                token = lexer.next();
                if (!expect(TokenType::LParen, "(")) break;
                token = lexer.next();

                // Парсим условие
                ConditionParser condParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                ConditionPtr cond = condParser.parse();
                if (!cond || !result.ok) break;

                if (!expect(TokenType::RParen, ")")) break;
                token = lexer.next();

                if (!expect(TokenType::LBrace, "{")) break;
                token = lexer.next();

                // Парсинг тела цикла
                // Аналогично if - используем стек для вложенных конструкций.
                IRBlock loopBody;
                std::vector<IRBlock*> blockStack;
                std::vector<std::shared_ptr<IRInstruction>> pendingIfStack;
                
                IRBlock* currentBlock = &loopBody;
                
                while (result.ok && token.type != TokenType::Eof) {
                    if (token.type == TokenType::RBrace) {
                        if (!pendingIfStack.empty()) {
                            auto pendingIf = pendingIfStack.back();
                            pendingIfStack.pop_back();
                            
                            if (!blockStack.empty()) {
                                currentBlock = blockStack.back();
                                blockStack.pop_back();
                            }
                            
                            token = lexer.next();
                            if (token.type == TokenType::Identifier && token.value == "else") {
                                token = lexer.next();
                                if (!expect(TokenType::LBrace, "{")) break;
                                token = lexer.next();
                                
                                blockStack.push_back(currentBlock);
                                pendingIfStack.push_back(pendingIf);
                                currentBlock = &pendingIf->elseBranch;
                            } else {
                                currentBlock->push_back(pendingIf);
                            }
                        } else {
                            break;
                        }
                    } else if (token.type == TokenType::Identifier) {
                        const std::string innerCmd = token.value;
                        const int innerLine = token.line;
                        const int innerCol = token.column;

                        if (innerCmd == "move_left") {
                            token = lexer.next();
                            if (!expect(TokenType::Semicolon, ";")) break;
                            currentBlock->push_back(IRInstruction::simple(IRType::MoveLeft, "", innerLine, innerCol));
                            token = lexer.next();
                        } else if (innerCmd == "move_right") {
                            token = lexer.next();
                            if (!expect(TokenType::Semicolon, ";")) break;
                            currentBlock->push_back(IRInstruction::simple(IRType::MoveRight, "", innerLine, innerCol));
                            token = lexer.next();
                        } else if (innerCmd == "write") {
                            token = lexer.next();
                            if (!expect(TokenType::StringLiteral, "символ для записи")) break;
                            const std::string symStr = token.value;
                            Symbol actualSym = (symStr == "blank") ? blankSymbol : symStr;
                            if (actualSym != blankSymbol && !alphabetSet.count(actualSym)) {
                                error(token.line, token.column, "Символ '" + symStr + "' не определён в алфавите");
                                break;
                            }
                            token = lexer.next();
                            if (!expect(TokenType::Semicolon, ";")) break;
                            currentBlock->push_back(IRInstruction::simple(IRType::Write, actualSym, innerLine, innerCol));
                            token = lexer.next();
                        } else if (innerCmd == "call") {
                            token = lexer.next();
                            if (!expect(TokenType::Identifier, "имя процедуры")) break;
                            const std::string pName = token.value;
                            if (!procedures.count(pName)) {
                                error(token.line, token.column, "Процедура '" + pName + "' не определена");
                                break;
                            }
                            token = lexer.next();
                            if (!expect(TokenType::Semicolon, ";")) break;
                            currentBlock->push_back(IRInstruction::simple(IRType::Call, pName, innerLine, innerCol));
                            token = lexer.next();
                        } else if (innerCmd == "if") {
                            token = lexer.next();
                            if (!expect(TokenType::LParen, "(")) break;
                            token = lexer.next();

                            ConditionParser innerCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                            ConditionPtr innerCond = innerCondParser.parse();
                            if (!innerCond || !result.ok) break;

                            if (!expect(TokenType::RParen, ")")) break;
                            token = lexer.next();
                            if (!expect(TokenType::LBrace, "{")) break;
                            token = lexer.next();

                            auto nestedIf = IRInstruction::ifElse(innerCond, IRBlock{}, IRBlock{}, innerLine, innerCol);
                            blockStack.push_back(currentBlock);
                            pendingIfStack.push_back(nestedIf);
                            currentBlock = &nestedIf->thenBranch;
                        } else if (innerCmd == "while") {
                            token = lexer.next();
                            if (!expect(TokenType::LParen, "(")) break;
                            token = lexer.next();

                            ConditionParser innerCondParser(lexer, token, alphabetSet, blankSymbol, result.diagnostics, result.ok);
                            ConditionPtr innerCond = innerCondParser.parse();
                            if (!innerCond || !result.ok) break;

                            if (!expect(TokenType::RParen, ")")) break;
                            token = lexer.next();
                            if (!expect(TokenType::LBrace, "{")) break;
                            token = lexer.next();

                            auto nestedWhile = IRInstruction::whileLoop(innerCond, IRBlock{}, innerLine, innerCol);
                            blockStack.push_back(currentBlock);
                            pendingIfStack.push_back(nestedWhile);
                            currentBlock = &nestedWhile->thenBranch;
                        } else {
                            error(innerLine, innerCol, "Неизвестная команда внутри while: '" + innerCmd + "'");
                            break;
                        }
                    } else {
                        error(token.line, token.column, "Ожидалась команда или '}'");
                        break;
                    }
                }

                if (!result.ok) break;
                token = lexer.next();

                // Создаём IR-инструкцию while с телом цикла
                auto whileInstr = IRInstruction::whileLoop(cond, std::move(loopBody), cmdLine, cmdCol);
                if (!addInstruction(whileInstr)) break;

            } else {
                // Неизвестная команда
                error(cmdLine, cmdCol, "Неизвестная команда: '" + cmd + "'");
                break;
            }
        } else if (token.type == TokenType::RBrace) {
            // Закрывающая скобка: конец тела процедуры
            if (!currentProc) {
                error(token.line, token.column, "Неожиданная '}'");
                break;
            }
            currentProc = nullptr;  // Выходим из текущей процедуры
            token = lexer.next();

        } else if (token.type == TokenType::Unknown) {
            // Неизвестный символ - ошибка лексера
            error(token.line, token.column, "Неожиданный символ: '" + token.value + "'");
            break;
        } else {
            // Ожидали команду, но получили что-то другое
            error(token.line, token.column, "Ожидалась команда");
            break;
        }
    }

    
    // Наличие незакрытой процедуры
    if (result.ok && currentProc) {
        error(currentProc->line, currentProc->column, "Процедура '" + currentProc->name + "' не закрыта (отсутствует '}')");
    }

    // Наличие процедуры main (точка входа)
    if (result.ok && !procedures.empty() && !procedures.count("main")) {
        result.diagnostics.push_back({DiagnosticLevel::Error, 1, 1, "Процедура 'main' не определена"});
        result.ok = false;
    }

    // Предупреждение: нет ни одной процедуры
    if (result.ok && procedures.empty()) {
        result.diagnostics.push_back({DiagnosticLevel::Warning, 1, 1, "Нет определённых процедур (нужна хотя бы 'main')"});
    }

    // Предупреждения о пропущенных командах
    if (result.ok && !alphabetDefined) {
        result.diagnostics.push_back({DiagnosticLevel::Warning, 1, 1, "Set_alphabet не определён"});
    }
    if (result.ok && !setupDefined) {
        result.diagnostics.push_back({DiagnosticLevel::Warning, 1, 1, "Setup не определён"});
    }

    // Генерация кода: IR → TransitionTable
    if (result.ok && procedures.count("main")) {
        IRBlock flatInstructions;
        std::unordered_set<std::string> callStack;  // Для обнаружения рекурсии
        
        // Рекурсивно разворачиваем все call в тело соответствующих процедур
        if (flattenProcedure("main", procedures, flatInstructions, callStack, result.diagnostics)) {
            // Генерируем переходы МТ из плоского IR-кода
            generateTransitions(flatInstructions, result.alphabet, result.table);
        } else {
            result.ok = false;
        }
    } else {
        // Нет main - создаём пустую таблицу (сразу останавливаемся)
        result.table.startState = 0;
        result.table.haltState = 0;
    }

    // Валидация сгенерированной таблицы переходов
    if (result.ok) {
        result.ok = result.table.validate(result.diagnostics);
    }

    return result;
}
