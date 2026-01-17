#include "App.h"

#include <algorithm>
#include <optional>
#include <cmath>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/System/Clock.hpp>


App::App() {
    // Моноширинный шрифт Consolas
    fontLoaded_ = font_.openFromFile("C:/Windows/Fonts/consola.ttf");

    // Загрузка preset.txt или использование шаблона по умолчанию
    const std::string defaultCode = 
        "Set_alphabet \"\";\n"
        "Setup \"\";\n"
        "\n"
        "proc main() {\n"
        "\n"
        "}\n";
    
    std::string presetCode;
    std::ifstream presetFile("preset.txt");
    if (presetFile.is_open()) {
        std::ostringstream ss;
        ss << presetFile.rdbuf();
        presetCode = ss.str();
        presetFile.close();
    }
    
    // Если preset.txt пуст или не существует - используем шаблон
    sourceCode_ = presetCode.empty() ? defaultCode : presetCode;
    
    // Разбиваем код на строки для редактора
    editorLines_.clear();
    std::istringstream iss(sourceCode_);
    std::string line;
    while (std::getline(iss, line)) {
        editorLines_.push_back(line);
    }
    if (editorLines_.empty()) {
        editorLines_.push_back("");
    }

    Compiler compiler;
    lastCompile_ = compiler.compile(sourceCode_);
    tableScrollX_ = 0.f;
    firstVisibleTransitionRow_ = 0;
}


App::Layout App::computeLayout(const sf::Vector2u& size) const {
    const float w = static_cast<float>(size.x);
    const float h = static_cast<float>(size.y);
    
    const float splitX = w * 0.5f;
    
    const float minTableH = 120.f;
    
    float controlH = std::max(48.f, h * 0.1f);
    
    float tapeH = h * 0.5f;
    if (tapeH > h - controlH - minTableH) {
        tapeH = std::max(80.f, h - controlH - minTableH);
    }
    
    float tableH = h - tapeH - controlH;
    if (tableH < minTableH) {
        // Если таблице не хватает места - уменьшаем панель управления
        const float deficit = minTableH - tableH;
        controlH = std::max(32.f, controlH - deficit);
        tableH = h - tapeH - controlH;
    }
    
    Layout layout{};
    layout.editor = {{0.f, 0.f}, {splitX, h}};
    layout.tape = {{splitX, 0.f}, {w - splitX, tapeH}};
    layout.controls = {{splitX, tapeH}, {w - splitX, controlH}};
    layout.table = {{splitX, tapeH + controlH}, {w - splitX, tableH}};
    
    return layout;
}

void App::handleEvent(const sf::Event& event, sf::RenderWindow& window) {
    // Закрытие окна
    if (event.is<sf::Event::Closed>()) {
        window.close();
        return;
    }

    // Изменение текста в редакторе (с точки зрения SFML - не нажаты Ctrl/Alt)
    if (const auto* text = event.getIf<sf::Event::TextEntered>()) {
        if (mode_ != AppMode::Running && text->unicode >= 32 && text->unicode <= 126) {
            handleEditorText(text->unicode);
        }
        return;
    }

    // Служебные и горячие клавиши
    if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        if (mode_ != AppMode::Running) {
            handleEditorKey(*key);
        }

        // Горячие клавиши управления симулятором (требуют Ctrl)
        if (key->control) {
            switch (key->code) {
            case sf::Keyboard::Key::C:
                requestCompile();
                break;
            case sf::Keyboard::Key::R:
                requestResetMachine();
                break;
            case sf::Keyboard::Key::Space:
                requestStep();
                break;
            case sf::Keyboard::Key::P:
                if (mode_ == AppMode::Running) {
                    requestPause();
                } else {
                    requestRun();
                }
                break;
            case sf::Keyboard::Key::S:
                requestStop();
                break;
            default:
                break;
            }
        }
    }

    // ЛКМ
    if (const auto* mouse = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (mouse->button == sf::Mouse::Button::Left) {
            const auto layout = computeLayout(window.getSize());
            const sf::Vector2f pos{static_cast<float>(mouse->position.x), static_cast<float>(mouse->position.y)};
            const sf::FloatRect controlsRect(layout.controls.pos, layout.controls.size);
            
            // Если клик в области панели управления - обрабатываем кнопки
            if (controlsRect.contains(pos)) {
                handleControlClick(pos, layout);
                return;
            }
        }
    }

    // Скроллинг
    if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        const auto layout = computeLayout(window.getSize());
        const sf::Vector2f pos{static_cast<float>(wheel->position.x), static_cast<float>(wheel->position.y)};
        
        const sf::FloatRect editorRect(layout.editor.pos, layout.editor.size);
        if (editorRect.contains(pos)) {
            // Скроллинг в редакторе
            const bool horizontal = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt);
            if (horizontal) {
                // Горизонтальный скролл (с зажатым Alt)
                const float padding = 8.f;
                const float vScrollbarW = editorVScrollbarWidth_;
                const float viewportW = layout.editor.size.x - vScrollbarW - padding * 2.f;
                const float step = 80.f;
                
                // Вычисляем ширину контента по самой длинной строке
                float maxWidth = 0.f;
                sf::Text measure(font_, "", static_cast<unsigned>(lineHeight_));
                for (const auto& line : editorLines_) {
                    measure.setString(line);
                    maxWidth = std::max(maxWidth, measure.getLocalBounds().size.x);
                }
                const float contentW = maxWidth + padding * 2.f;
                clampEditorScroll(viewportW, contentW);
                editorScrollX_ -= wheel->delta * step;
                clampEditorScroll(viewportW, contentW);
            } else {
                // Вертикальный скролл (по умолчанию)
                if (wheel->delta < 0) {
                    if (firstVisibleLine_ + 1 < editorLines_.size()) {
                        firstVisibleLine_ = std::min(editorLines_.size() - 1, firstVisibleLine_ + 1);
                    }
                } else if (wheel->delta > 0 && firstVisibleLine_ > 0) {
                    firstVisibleLine_--;
                }
            }
        } else {
            const sf::FloatRect tapeRect(layout.tape.pos, layout.tape.size);
            // Скроллинг ленты
            if (tapeRect.contains(pos)) {
                scrollTape(wheel->delta > 0 ? -1 : 1);
            } else {
                const sf::FloatRect tableRect(layout.table.pos, layout.table.size);
                // Скроллинг таблицы переходов
                if (tableRect.contains(pos)) {
                    const auto states = lastCompile_.table.states();
                    const std::size_t totalRows = std::max<std::size_t>(1, states.size());
                    const float headerH = 28.f;
                    const float vScrollbarW = 8.f;
                    const float viewportW = layout.table.size.x - 2.f * 8.f - vScrollbarW;
                    const bool horizontal = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::RAlt);
                    
                    if (horizontal) {
                        const float step = 60.f;
                        const auto alphabet = !lastCompile_.alphabet.empty() ? lastCompile_.alphabet : lastCompile_.table.alphabet();
                        clampTableScroll(viewportW, alphabet.size() + 1);
                        tableScrollX_ -= wheel->delta * step;
                        clampTableScroll(viewportW, alphabet.size() + 1);
                    } else {
                        const float maxVisibleF = (layout.table.size.y - headerH - 8.f * 2.f) / tableRowHeight_;
                        const std::size_t maxVisible = static_cast<std::size_t>(std::max(1.f, std::floor(maxVisibleF)));
                        if (wheel->delta < 0 && firstVisibleTransitionRow_ + maxVisible < totalRows) {
                            firstVisibleTransitionRow_++;
                        } else if (wheel->delta > 0 && firstVisibleTransitionRow_ > 0) {
                            firstVisibleTransitionRow_--;
                        }
                    }
                }
            }
        }
    }
}


void App::update(float) {
    if (mode_ == AppMode::Running) {
        requestStep();
    }
}


void App::render(sf::RenderWindow& window) {
    const auto layout = computeLayout(window.getSize());

    // Подготовка прямоугольников 
    // Фон редактора 
    sf::RectangleShape editorBg;
    editorBg.setPosition(layout.editor.pos);
    editorBg.setSize(layout.editor.size);
    editorBg.setFillColor(sf::Color(35, 35, 45));

    // Фон ленты
    sf::RectangleShape tapeBg;
    tapeBg.setPosition(layout.tape.pos);
    tapeBg.setSize(layout.tape.size);
    tapeBg.setFillColor(sf::Color(45, 55, 70));

    // Фон панели управления
    sf::RectangleShape controlsBg;
    controlsBg.setPosition(layout.controls.pos);
    controlsBg.setSize(layout.controls.size);
    controlsBg.setFillColor(sf::Color(60, 55, 75));

    // Фон таблицы
    sf::RectangleShape tableBg;
    tableBg.setPosition(layout.table.pos);
    tableBg.setSize(layout.table.size);
    tableBg.setFillColor(sf::Color(50, 45, 60));

    
    // Рисуем
    window.clear(sf::Color(25, 25, 30));
    
    window.draw(tableBg);
    renderTable(window, layout);

    window.draw(controlsBg);
    renderControls(window, layout);

    window.draw(tapeBg);
    renderTape(window, layout);

    window.draw(editorBg);
    renderEditor(window, layout);


    window.display();
}


void App::renderTable(sf::RenderWindow& window, const Layout& layout) {
    if (!fontLoaded_) {
        return;
    }

    // Сохраняем текущий view для восстановления после отрисовки
    const auto prevView = window.getView();
    const auto winSize = window.getSize();
    
    // Создаём отдельный view для области таблицы (clipping)
    sf::View tableView(sf::FloatRect{sf::Vector2f{layout.table.pos.x, layout.table.pos.y}, sf::Vector2f{layout.table.size.x, layout.table.size.y}});
    tableView.setViewport(sf::FloatRect{
        sf::Vector2f{layout.table.pos.x / static_cast<float>(winSize.x), layout.table.pos.y / static_cast<float>(winSize.y)},
        sf::Vector2f{layout.table.size.x / static_cast<float>(winSize.x), layout.table.size.y / static_cast<float>(winSize.y)}});
    window.setView(tableView);

    const float padding = 8.f;
    const float headerH = 28.f;
    const float vScrollbarW = 8.f;
    
    // Получаем алфавит и состояния из результата компиляции
    const auto alphabet = !lastCompile_.alphabet.empty() ? lastCompile_.alphabet : lastCompile_.table.alphabet();
    const auto states = lastCompile_.table.states();

    const float viewportW = layout.table.size.x - padding * 2.f - vScrollbarW;
    clampTableScroll(viewportW, alphabet.size() + 1);

    // Заголовок таблицы
    const float colW = tableColWidth_;
    sf::Text text(font_, "", static_cast<unsigned>(lineHeight_));
    text.setFillColor(sf::Color(220, 220, 230));

    // Отрисовываем только видимые колонки
    const std::size_t totalCols = alphabet.size() + 1;  // +1 для колонки State
    const std::size_t firstCol = static_cast<std::size_t>(std::max(0.f, std::floor(tableScrollX_ / colW)));
    const std::size_t maxColsVisible = static_cast<std::size_t>(std::ceil(viewportW / colW)) + 2;
    const std::size_t endCol = std::min(totalCols, firstCol + maxColsVisible);

    // Фон заголовка
    sf::RectangleShape headerBg;
    headerBg.setPosition({layout.table.pos.x, layout.table.pos.y});
    headerBg.setSize({layout.table.size.x, headerH + padding});
    headerBg.setFillColor(sf::Color(70, 65, 85));
    window.draw(headerBg);

    // Отрисовка заголовков колонок
    text.setStyle(sf::Text::Bold);
    const float baseY = layout.table.pos.y + padding + (headerH - lineHeight_) * 0.5f;
    for (std::size_t col = firstCol; col < endCol; col++) {
        if (col == 0) {
            text.setString("State");  // Первая колонка - номер состояния
        } else {
            // Остальные - символы алфавита
            const std::size_t symIdx = col - 1;
            const std::string dispSym = (alphabet[symIdx] == " ") ? std::string("<sp>") : alphabet[symIdx];
            text.setString(dispSym);
        }

        // Позиция с учётом горизонтального скролла
        const float x = layout.table.pos.x + padding + colW * static_cast<float>(col) + 6.f - tableScrollX_;
        if (x > layout.table.pos.x + padding - colW && x < layout.table.pos.x + padding + viewportW) {
            text.setPosition({x, baseY});
            window.draw(text);
        }
    }

    // Отрисовка строк таблицы
    const std::size_t totalRows = std::max<std::size_t>(1, states.size());
    const float maxVisibleF = (layout.table.size.y - headerH - padding * 3.f - 8.f) / tableRowHeight_;
    const std::size_t maxVisible = static_cast<std::size_t>(std::max(1.f, std::floor(maxVisibleF)));
    const std::size_t startRow = std::min(firstVisibleTransitionRow_, (totalRows > 0) ? totalRows - 1 : 0);
    const std::size_t endRow = std::min(totalRows, startRow + maxVisible);

    float rowY = layout.table.pos.y + padding + headerH;
    text.setStyle(sf::Text::Regular);

    for (std::size_t r = startRow; r < endRow; r++) {
        // Чередующийся фон
        const bool alt = (r % 2) == 0;
        sf::RectangleShape rowBg;
        rowBg.setPosition({layout.table.pos.x, rowY});
        rowBg.setSize({layout.table.size.x, tableRowHeight_});
        rowBg.setFillColor(alt ? sf::Color(60, 55, 70) : sf::Color(55, 50, 65));
        window.draw(rowBg);

        // Отрисовка ячеек строки
        for (std::size_t col = firstCol; col < endCol; col++) {
            const bool isStateCol = (col == 0);
            if (isStateCol) {
                // Первая колонка - номер состояния (q0, q1, ...)
                text.setString("q" + std::to_string(states.empty() ? 0 : states[r]));
            } else {
                // Ячейка перехода
                const std::size_t symIdx = col - 1;
                const Symbol& sym = alphabet[symIdx];
                const Transition* tr = nullptr;
                if (!states.empty()) {
                    tr = lastCompile_.table.get(states[r], sym);
                }
                
                // Состояние остановки - "halt"
                const bool isHalt = (!states.empty() && states[r] == lastCompile_.table.haltState);
                std::string cell = isHalt ? std::string("halt") : std::string("-");
                
                // Если есть переход - форматируем как "qX, символ, L/R/S"
                if (!isHalt && tr) {
                    cell = "q" + std::to_string(tr->nextState) + ", " + tr->writeSymbol + ", " +
                        (tr->move == Move::Left ? "L" : tr->move == Move::Right ? "R" : "S");
                }
                text.setString(cell);
            }

            // Позиция с учётом скролла
            const float x = layout.table.pos.x + padding + colW * static_cast<float>(col) + 6.f - tableScrollX_;
            if (x > layout.table.pos.x + padding - colW && x < layout.table.pos.x + padding + viewportW) {
                text.setPosition({x, rowY + (tableRowHeight_ - lineHeight_) * 0.5f});
                window.draw(text);
            }
        }

        rowY += tableRowHeight_;
    }

    // Вертикальный скроллбар
    const float sbX = layout.table.pos.x + layout.table.size.x - padding - vScrollbarW;
    const float sbY = layout.table.pos.y + padding + headerH;
    const float hTrackH = 8.f;
    const float sbH = layout.table.size.y - padding * 3.f - headerH - hTrackH;
    
    // Трек скроллбара
    sf::RectangleShape track;
    track.setPosition({sbX, sbY});
    track.setSize({vScrollbarW, sbH});
    track.setFillColor(sf::Color(60, 60, 70));
    window.draw(track);

    // Ползунок
    if (totalRows > maxVisible) {
        const float ratio = static_cast<float>(maxVisible) / static_cast<float>(totalRows);
        const float thumbH = std::max(20.f, sbH * ratio);
        const float maxScroll = static_cast<float>(totalRows - maxVisible);
        const float t = static_cast<float>(startRow) / maxScroll;
        const float thumbY = sbY + t * (sbH - thumbH);

        sf::RectangleShape thumb;
        thumb.setPosition({sbX, thumbY});
        thumb.setSize({vScrollbarW, thumbH});
        thumb.setFillColor(sf::Color(180, 180, 200));
        window.draw(thumb);
    }

    // Горизонтальный скроллбар
    const float hTrackHeight = 8.f;
    const float hTrackY = layout.table.pos.y + layout.table.size.y - padding - hTrackHeight;
    const float hTrackX = layout.table.pos.x + padding;
    const float hTrackW = viewportW;
    sf::RectangleShape hTrack;
    hTrack.setPosition({hTrackX, hTrackY});
    hTrack.setSize({hTrackW, hTrackHeight});
    hTrack.setFillColor(sf::Color(60, 60, 70));
    window.draw(hTrack);

    // Ползунок горизонтального скроллбара
    const float contentW = static_cast<float>(alphabet.size() + 1) * colW;
    const float hThumbRatio = (contentW > 0) ? hTrackW / contentW : 1.f;
    const float hThumbW = std::max(24.f, hTrackW * std::min(1.f, hThumbRatio));
    const float hDenom = std::max(1.f, contentW - hTrackW);
    const float ht = tableScrollX_ / hDenom;
    const float hThumbX = hTrackX + ht * (hTrackW - hThumbW);
    sf::RectangleShape hThumb;
    hThumb.setPosition({hThumbX, hTrackY});
    hThumb.setSize({hThumbW, hTrackHeight});
    hThumb.setFillColor(sf::Color(180, 180, 200));
    window.draw(hThumb);

    // Восстанавливаем исходный view
    window.setView(prevView);
}


// handleEditorText - Обработка текстового ввода в редакторе
void App::handleEditorText(char32_t unicode) {
    // Игнорируем непечатные символы
    if (unicode < 32 || unicode > 126) {
        return;
    }
    std::string ch;
    ch.push_back(static_cast<char>(unicode));
    auto& line = editorLines_[cursorRow_];
    line.insert(cursorCol_, ch);       // Вставляем символ в текущую позицию
    cursorCol_ += ch.size();           // Перемещаем курсор вправо
    markEdited();                      // Помечаем код как изменённый
}


// handleEditorKey - Обработка нажатий клавиш в редакторе
void App::handleEditorKey(const sf::Event::KeyPressed& key) {
    auto& line = editorLines_[cursorRow_];
    switch (key.code) {
    
    // TAB - вставка 4 пробелов
    case sf::Keyboard::Key::Tab: {
        line.insert(cursorCol_, "    ");
        cursorCol_ += 4;
        markEdited();
        break;
    }
    
    // ENTER - создание новой строки
    case sf::Keyboard::Key::Enter: {
        // Текст после курсора переносится на новую строку
        std::string newLine = line.substr(cursorCol_);
        line.erase(cursorCol_);
        editorLines_.insert(editorLines_.begin() + static_cast<long long>(cursorRow_) + 1, std::move(newLine));
        cursorRow_++;
        cursorCol_ = 0;
        markEdited();
        break;
    }
    
    // BACKSPACE - удаление символа перед курсором
    case sf::Keyboard::Key::Backspace: {
        if (cursorCol_ > 0) {
            // Удаляем символ перед курсором
            line.erase(cursorCol_ - 1, 1);
            cursorCol_--;
        } else if (cursorRow_ > 0) {
            // Курсор в начале строки - объединяем с предыдущей
            cursorCol_ = editorLines_[cursorRow_ - 1].size();
            editorLines_[cursorRow_ - 1] += line;
            editorLines_.erase(editorLines_.begin() + static_cast<long long>(cursorRow_));
            cursorRow_--;
        }
        markEdited();
        break;
    }
    
    // DELETE - удаление символа после курсора
    case sf::Keyboard::Key::Delete: {
        if (cursorCol_ < line.size()) {
            // Удаляем символ после курсора
            line.erase(cursorCol_, 1);
        } else if (cursorRow_ + 1 < editorLines_.size()) {
            // Курсор в конце строки - объединяем со следующей
            line += editorLines_[cursorRow_ + 1];
            editorLines_.erase(editorLines_.begin() + static_cast<long long>(cursorRow_) + 1);
        }
        markEdited();
        break;
    }
    
    // СТРЕЛКА ВЛЕВО - перемещение курсора влево
    case sf::Keyboard::Key::Left: {
        if (key.control) {
            // Ctrl+Left: переход к началу предыдущего слова
            if (cursorCol_ == 0 && cursorRow_ > 0) {
                // Переход на конец предыдущей строки
                cursorRow_--;
                cursorCol_ = editorLines_[cursorRow_].size();
            }
            if (cursorCol_ > 0) {
                const std::string& ln = editorLines_[cursorRow_];
                std::size_t pos = cursorCol_;
                // Пропускаем пробелы назад
                while (pos > 0 && std::isspace(static_cast<unsigned char>(ln[pos - 1]))) {
                    pos--;
                }
                // Пропускаем слово назад
                while (pos > 0 && !std::isspace(static_cast<unsigned char>(ln[pos - 1]))) {
                    pos--;
                }
                cursorCol_ = pos;
            }
        } else {
            // Обычное перемещение на один символ
            if (cursorCol_ > 0) {
                cursorCol_--;
            } else if (cursorRow_ > 0) {
                cursorRow_--;
                cursorCol_ = editorLines_[cursorRow_].size();
            }
        }
        break;
    }
    
    // СТРЕЛКА ВПРАВО - перемещение курсора вправо
    case sf::Keyboard::Key::Right: {
        if (key.control) {
            // Ctrl+Right: переход к началу следующего слова
            const std::string& ln = editorLines_[cursorRow_];
            std::size_t pos = cursorCol_;
            // Пропускаем текущее слово
            while (pos < ln.size() && !std::isspace(static_cast<unsigned char>(ln[pos]))) {
                pos++;
            }
            // Пропускаем пробелы
            while (pos < ln.size() && std::isspace(static_cast<unsigned char>(ln[pos]))) {
                pos++;
            }
            if (pos >= ln.size() && cursorRow_ + 1 < editorLines_.size()) {
                // Переход на начало следующей строки
                cursorRow_++;
                cursorCol_ = 0;
            } else {
                cursorCol_ = pos;
            }
        } else {
            // Обычное перемещение на один символ
            if (cursorCol_ < line.size()) {
                cursorCol_++;
            } else if (cursorRow_ + 1 < editorLines_.size()) {
                cursorRow_++;
                cursorCol_ = 0;
            }
        }
        break;
    }
    
    // СТРЕЛКА ВВЕРХ - перемещение на строку выше
    case sf::Keyboard::Key::Up: {
        if (cursorRow_ > 0) {
            cursorRow_--;
            // Ограничиваем позицию курсора длиной новой строки
            cursorCol_ = std::min(cursorCol_, editorLines_[cursorRow_].size());
        }
        // Скроллинг, если курсор ушёл выше видимой области
        if (cursorRow_ < firstVisibleLine_ && firstVisibleLine_ > 0) {
            firstVisibleLine_--;
        }
        break;
    }
    
    // СТРЕЛКА ВНИЗ - перемещение на строку ниже
    case sf::Keyboard::Key::Down: {
        if (cursorRow_ + 1 < editorLines_.size()) {
            cursorRow_++;
            cursorCol_ = std::min(cursorCol_, editorLines_[cursorRow_].size());
        }
        break;
    }
    
    // HOME - переход в начало строки
    case sf::Keyboard::Key::Home: {
        cursorCol_ = 0;
        break;
    }
    
    // END - переход в конец строки
    case sf::Keyboard::Key::End: {
        cursorCol_ = line.size();
        break;
    }
    
    default:
        break;
    }

    clampCursor();  // Гарантируем корректность позиции курсора
}


// clampCursor - Ограничение позиции курсора
void App::clampCursor() {
    if (editorLines_.empty()) {
        editorLines_.push_back("");  // Гарантируем минимум одну строку
    }
    cursorRow_ = std::min(cursorRow_, editorLines_.size() - 1);
    cursorCol_ = std::min(cursorCol_, editorLines_[cursorRow_].size());
}


// rebuildSourceFromLines - Сборка исходного кода из строк редактора
void App::rebuildSourceFromLines() {
    sourceCode_.clear();
    for (std::size_t i = 0; i < editorLines_.size(); i++) {
        sourceCode_ += editorLines_[i];
        if (i + 1 < editorLines_.size()) {
            sourceCode_ += '\n';
        }
    }
}

// scrollTape - Прокрутка ленты
void App::scrollTape(int deltaCells) {
    tapeOffset_ += deltaCells;
    clampTapeOffsetToContent(tapeVisibleCells_);
}

// ensureTapeHeadVisible - Обеспечение видимости головки
void App::ensureTapeHeadVisible() {
    const long long headPos = tm_.head();
    if (headPos < tapeOffset_) {
        // Головка ушла влево за пределы видимой области
        tapeOffset_ = headPos - 1;
    } else if (headPos >= tapeOffset_ + static_cast<long long>(tapeVisibleCells_)) {
        // Головка ушла вправо за пределы видимой области
        tapeOffset_ = headPos - static_cast<long long>(tapeVisibleCells_ / 2);
    }

    clampTapeOffsetToContent(tapeVisibleCells_);
}


// clampTableScroll - Ограничение горизонтального скролла таблицы
void App::clampTableScroll(float viewportWidth, std::size_t columns) {
    const float contentW = static_cast<float>(columns) * tableColWidth_;
    const float maxScroll = std::max(0.f, contentW - viewportWidth);
    if (tableScrollX_ < 0.f) tableScrollX_ = 0.f;
    if (tableScrollX_ > maxScroll) tableScrollX_ = maxScroll;
}

// clampEditorScroll - Ограничение горизонтального скролла редактора
void App::clampEditorScroll(float viewportWidth, float contentWidth) {
    const float maxScroll = std::max(0.f, contentWidth - viewportWidth);
    if (editorScrollX_ < 0.f) editorScrollX_ = 0.f;
    if (editorScrollX_ > maxScroll) editorScrollX_ = maxScroll;
}


// clampTapeOffsetToContent - Ограничение смещения ленты
void App::clampTapeOffsetToContent(std::size_t visibleCells) {
    const long long margin = 20;  // Отступ за пределы записанных ячеек
    
    // Получаем границы ленты (min, max индексы, в которых записано что-то не пустое)
    const auto bounds = tm_.tape().bounds(tm_.head());
    long long minView = bounds.first - margin;
    long long maxView = bounds.second + margin;
    if (maxView < minView) {
        maxView = minView;
    }

    // Максимальное смещение, при котором ещё видны ячейки
    long long maxOffset = maxView - static_cast<long long>(visibleCells) + 1;
    if (maxOffset < minView) {
        maxOffset = minView;
    }

    // Ограничиваем смещение
    if (tapeOffset_ < minView) {
        tapeOffset_ = minView;
    } else if (tapeOffset_ > maxOffset) {
        tapeOffset_ = maxOffset;
    }
}


// buildControlButtons - Создание спецификаций кнопок управления
std::vector<App::ControlButtonSpec> App::buildControlButtons(const Layout& layout) const {
    const float padding = 8.f;
    const float spacing = 8.f;
    const float btnW = 96.f;
    const float btnH = 32.f;
    float x = layout.controls.pos.x + padding;
    const float y = layout.controls.pos.y + padding;

    std::vector<ControlButtonSpec> out;
    out.reserve(5);

    // Лямбда для добавления кнопки
    auto push = [&](std::string label, bool enabled) {
        ControlButtonSpec spec{};
        spec.rect = sf::FloatRect{sf::Vector2f{x, y}, sf::Vector2f{btnW, btnH}};
        spec.label = std::move(label);
        spec.enabled = enabled;
        out.push_back(std::move(spec));
        x += btnW + spacing;  // Следующая кнопка правее
    };

    // Определяем текущее состояние приложения
    const bool running = (mode_ == AppMode::Running);
    const bool paused = (mode_ == AppMode::Paused);
    const bool halted = (mode_ == AppMode::Halted);
    const bool canRunBase = hasValidTable() && !sourceDirty_;

    // Кнопки
    push("Compile", mode_ != AppMode::Running);                         // Компиляция (не во время выполнения)
    push("Reset", hasValidTable());                                     // Сброс (если есть таблица)
    push("Step", hasValidTable() && !running && !halted);               // Шаг (если не выполняется и не остановлено)
    push(running ? "Pause" : "Run", canRunBase || running || paused);   // Run/Pause
    push("Stop", running || paused);                                    // Стоп (во время выполнения)

    return out;
}


// handleControlClick - Обработка клика по кнопке управления
void App::handleControlClick(const sf::Vector2f& pos, const Layout& layout) {
    const auto buttons = buildControlButtons(layout);
    for (std::size_t i = 0; i < buttons.size(); i++) {
        // Кнопка неактивна
        if (!buttons[i].enabled) {
            continue;
        }

        // Клик не попал в кнопку
        if (!buttons[i].rect.contains(pos)) {
            continue;
        }

        switch (i) {
        case 0:
            requestCompile();
            return;
        case 1:
            requestResetMachine();
            return;
        case 2:
            requestStep();
            return;
        case 3:
            if (mode_ == AppMode::Running) {
                requestPause();
            } else {
                requestRun();
            }
            return;
        case 4:
            requestStop();
            return;
        default:
            break;
        }
    }
}

// renderTape - Отрисовка ленты машины Тьюринга
void App::renderTape(sf::RenderWindow& window, const Layout& layout) {
    if (!fontLoaded_) {
        return;
    }

    const float padding = tapePadding_;
    const float cellW = tapeCellWidth_;
    const float cellH = tapeCellHeight_;
    
    // Вычисляем количество видимых ячеек
    const std::size_t visibleCells = static_cast<std::size_t>(std::max(1.f, std::floor((layout.tape.size.x - 2.f * padding) / cellW)));
    tapeVisibleCells_ = visibleCells;

    clampTapeOffsetToContent(visibleCells);

    // Вычисляем границы для скроллбара
    const auto bounds = tm_.tape().bounds(tm_.head());
    const long long margin = 20;
    const long long minView = bounds.first - margin;
    const long long maxView = bounds.second + margin;
    const long long maxOffset = std::max(minView, maxView - static_cast<long long>(visibleCells) + 1);

    // Текст для отображения символов в ячейках
    sf::Text cellText(font_, "", static_cast<unsigned>(cellH * 0.55f));
    cellText.setFillColor(sf::Color(230, 230, 230));

    const float startX = layout.tape.pos.x + padding;
    const float startY = layout.tape.pos.y + padding;


    // Отрисовка ячеек
    for (std::size_t i = 0; i < visibleCells; i++) {
        const long long cellIndex = tapeOffset_ + static_cast<long long>(i);
        const Symbol sym = tm_.tape().get(cellIndex);
        
        std::string text = sym;
        if (text.empty()) {
            text = " ";  // Пустой символ - пробел
        }
        
        // Ограничиваем длинные символы
        const std::size_t maxChars = 4;
        if (text.size() > maxChars) {
            text = text.substr(0, maxChars - 2) + "...";
        }

        const float x = startX + static_cast<float>(i) * cellW;
        
        // Рисуем ячейку
        sf::RectangleShape box;
        box.setPosition({x, startY});
        box.setSize({cellW - 4.f, cellH});
        
        // Головка выделяется оранжевым цветом
        const bool isHead = (cellIndex == tm_.head());
        box.setFillColor(isHead ? sf::Color(200, 120, 60) : sf::Color(70, 80, 100));
        box.setOutlineThickness(1.f);
        box.setOutlineColor(sf::Color(30, 30, 40));
        window.draw(box);

        // Рисуем символ в ячейке
        cellText.setString(text);
        const sf::FloatRect bounds = cellText.getLocalBounds();
        const float tx = x + (cellW - bounds.size.x) * 0.5f - bounds.position.x;
        const float ty = startY + (cellH - bounds.size.y) * 0.5f - bounds.position.y;
        cellText.setPosition({tx, ty});
        window.draw(cellText);
    }

    // Разделительная линия под ячейками
    const float markerY = startY + cellH + 4.f;
    sf::RectangleShape marker;
    marker.setPosition({layout.tape.pos.x + padding, markerY});
    marker.setSize({layout.tape.size.x - 2.f * padding, 2.f});
    marker.setFillColor(sf::Color(100, 120, 160));
    window.draw(marker);

    // Индексы ячеек
    sf::Text idxText(font_, "", static_cast<unsigned>(cellH * 0.28f));
    idxText.setFillColor(sf::Color(200, 200, 210));
    const float idxY = markerY + 6.f;
    for (std::size_t i = 0; i < visibleCells; i++) {
        const long long cellIndex = tapeOffset_ + static_cast<long long>(i);
        idxText.setString(std::to_string(cellIndex));
        const auto b = idxText.getLocalBounds();
        const float x = startX + static_cast<float>(i) * cellW;
        const float tx = x + (cellW - b.size.x) * 0.5f - b.position.x;
        idxText.setPosition({tx, idxY});
        window.draw(idxText);
    }

    // Горизонтальный скроллбар
    const float trackH = 8.f;
    const float trackY = layout.tape.pos.y + layout.tape.size.y - trackH - 4.f;
    const float trackW = layout.tape.size.x - 2.f * padding;
    
    // Трек скроллбара
    sf::RectangleShape track;
    track.setPosition({layout.tape.pos.x + padding, trackY});
    track.setSize({trackW, trackH});
    track.setFillColor(sf::Color(60, 60, 70));
    window.draw(track);

    // Ползунок
    const long long span = maxView - minView + 1;
    const float thumbRatio = (span > 0) ? static_cast<float>(visibleCells) / static_cast<float>(span) : 1.f;
    const float thumbW = std::max(24.f, trackW * std::min(1.f, thumbRatio));
    const float denom = static_cast<float>(std::max<long long>(1, maxOffset - minView));
    const float t = (tapeOffset_ - minView) / denom;
    const float thumbX = layout.tape.pos.x + padding + t * (trackW - thumbW);
    sf::RectangleShape thumb;
    thumb.setPosition({thumbX, trackY});
    thumb.setSize({thumbW, trackH});
    thumb.setFillColor(sf::Color(180, 180, 200));
    window.draw(thumb);
}

// renderControls - Отрисовка панели управления
void App::renderControls(sf::RenderWindow& window, const Layout& layout) {
    if (!fontLoaded_) {
        return;
    }

    const float padding = 8.f;
    auto buttons = buildControlButtons(layout);

    sf::Text text(font_, "", static_cast<unsigned>(lineHeight_));
    text.setFillColor(sf::Color(230, 230, 240));

    // Рисуем кнопки
    for (const auto& btn : buttons) {
        sf::RectangleShape box;
        box.setPosition(btn.rect.position);
        box.setSize(btn.rect.size);
        
        const bool isRun = (btn.label == "Run");
        const bool isPause = (btn.label == "Pause");
        const sf::Color base = isRun ? sf::Color(70, 120, 90) : (isPause ? sf::Color(140, 110, 70) : sf::Color(80, 90, 110));
        const sf::Color disabled(60, 60, 70);
        
        box.setFillColor(btn.enabled ? base : disabled);
        box.setOutlineThickness(1.f);
        box.setOutlineColor(sf::Color(30, 30, 40));
        window.draw(box);

        // Текст кнопки
        text.setString(btn.label);
        const auto bounds = text.getLocalBounds();
        const float tx = btn.rect.position.x + (btn.rect.size.x - bounds.size.x) * 0.5f - bounds.position.x;
        const float ty = btn.rect.position.y + (btn.rect.size.y - bounds.size.y) * 0.5f - bounds.position.y;
        text.setPosition({tx, ty});
        window.draw(text);
    }

    // Статус справа
    std::string modeStr = "Mode: ";
    switch (mode_) {
    case AppMode::IdleEditing: modeStr += "Editing"; break;
    case AppMode::CompiledOk: modeStr += "Compiled"; break;
    case AppMode::CompileError: modeStr += "Compile Error"; break;
    case AppMode::ReadyToRun: modeStr += "Ready"; break;
    case AppMode::Running: modeStr += "Running"; break;
    case AppMode::Paused: modeStr += "Paused"; break;
    case AppMode::Halted: modeStr += "Halted"; break;
    }
    
    // Если код изменён после компиляции - (dirty)
    if (sourceDirty_) {
        modeStr += " (dirty)";
    }

    text.setString(modeStr);
    const auto bounds = text.getLocalBounds();
    const float statusX = layout.controls.pos.x + layout.controls.size.x - padding - bounds.size.x - bounds.position.x;
    const float statusY = layout.controls.pos.y + padding - bounds.position.y;
    text.setPosition({statusX, statusY});
    window.draw(text);
}

// renderEditor - Отрисовка текстового редактора
void App::renderEditor(sf::RenderWindow& window, const Layout& layout) {
    if (!fontLoaded_) {
        return;
    }

    const float padding = 8.f;
    const float vScrollbarW = editorVScrollbarWidth_;    // Ширина вертикального скроллбара
    const float hScrollbarH = editorHScrollbarHeight_;   // Высота горизонтального скроллбара
    const float gutterW = editorLineNumberWidth_;        // Ширина области номеров строк

    sf::Text lineText(font_, "", static_cast<unsigned>(lineHeight_));
    lineText.setFillColor(sf::Color(220, 220, 220));

    
    // Вычисление размеров контента
    float maxLineWidth = 0.f;
    for (const auto& line : editorLines_) {
        lineText.setString(line);
        maxLineWidth = std::max(maxLineWidth, lineText.getLocalBounds().size.x);
    }

    const float contentWidth = maxLineWidth + padding * 2.f;
    const float contentHeight = layout.editor.size.y - hScrollbarH;
    const float viewportWidth = layout.editor.size.x - vScrollbarW - padding * 2.f - gutterW;
    const float viewportHeight = contentHeight - padding;

    // Вычисляем диапазон видимых строк
    const std::size_t totalLines = std::max<std::size_t>(1, editorLines_.size());
    const std::size_t maxVisible = static_cast<std::size_t>(std::max(1.f, std::floor(viewportHeight / lineHeight_)));
    const std::size_t startLine = std::min(firstVisibleLine_, totalLines - 1);
    const std::size_t endLine = std::min(totalLines, startLine + maxVisible + 1);

    clampEditorScroll(viewportWidth, contentWidth);

    
    // Автоматический скролл к курсору
    // Гарантируем, что курсор всегда виден в горизонтальной области
    float caretPixelX = 0.f;
    if (!editorLines_.empty()) {
        const std::string& caretLine = editorLines_[cursorRow_];
        const std::size_t caretCol = std::min(cursorCol_, caretLine.size());
        lineText.setString(caretLine.substr(0, caretCol));
        caretPixelX = lineText.getLocalBounds().size.x;
        
        const float caretPad = 4.f;
        const float desiredLeft = caretPixelX - caretPad;
        const float desiredRight = caretPixelX + caretPad;
        
        if (desiredLeft < editorScrollX_) {
            editorScrollX_ = std::max(0.f, desiredLeft);
        } else if (desiredRight > editorScrollX_ + viewportWidth) {
            editorScrollX_ = desiredRight - viewportWidth;
        }
        clampEditorScroll(viewportWidth, contentWidth);
    }

    const auto prevView = window.getView();
    const auto winSize = window.getSize();

    // Область номеров строк
    sf::RectangleShape gutter;
    gutter.setPosition({layout.editor.pos.x, layout.editor.pos.y});
    gutter.setSize({gutterW, layout.editor.size.y - hScrollbarH});
    gutter.setFillColor(sf::Color(40, 40, 50));
    window.draw(gutter);

    // Текстовая область (с clipping)    
    // Создаём отдельный view для области текста (исключая gutter)
    sf::View editorView(sf::FloatRect{sf::Vector2f{layout.editor.pos.x + gutterW, layout.editor.pos.y},
                                      sf::Vector2f{layout.editor.size.x - vScrollbarW - gutterW, layout.editor.size.y - hScrollbarH}});
    editorView.setViewport(sf::FloatRect{
        sf::Vector2f{(layout.editor.pos.x + gutterW) / static_cast<float>(winSize.x),
                     layout.editor.pos.y / static_cast<float>(winSize.y)},
        sf::Vector2f{(layout.editor.size.x - vScrollbarW - gutterW) / static_cast<float>(winSize.x),
                     (layout.editor.size.y - hScrollbarH) / static_cast<float>(winSize.y)}});
    window.setView(editorView);

    // Отрисовка строк кода
    float y = layout.editor.pos.y + padding;
    for (std::size_t i = startLine; i < endLine; i++) {
        lineText.setString(editorLines_[i]);
        lineText.setPosition({layout.editor.pos.x + padding + gutterW - editorScrollX_, y});
        window.draw(lineText);
        y += lineHeight_;
    }

    // Курсор    
    const std::size_t cursorLineOnScreen = (cursorRow_ >= startLine) ? cursorRow_ - startLine : static_cast<std::size_t>(-1);
    if (cursorLineOnScreen != static_cast<std::size_t>(-1) && cursorLineOnScreen < maxVisible + 1) {
        sf::RectangleShape caret;
        caret.setFillColor(sf::Color(200, 200, 255));
        const float caretX = layout.editor.pos.x + padding + gutterW + caretPixelX - editorScrollX_;
        const float caretY = layout.editor.pos.y + padding + static_cast<float>(cursorLineOnScreen) * lineHeight_;
        caret.setPosition({caretX, caretY});
        caret.setSize({2.f, lineHeight_});
        window.draw(caret);
    }

    window.setView(prevView);  // Восстанавливаем view

    
    // Номера строк
    sf::Text numText(font_, "", static_cast<unsigned>(lineHeight_));
    numText.setFillColor(sf::Color(140, 140, 160));
    float ny = layout.editor.pos.y + padding;
    for (std::size_t i = startLine; i < endLine; i++) {
        numText.setString(std::to_string(i + 1));
        const auto nb = numText.getLocalBounds();
        const float nx = layout.editor.pos.x + gutterW - padding - nb.size.x - nb.position.x;
        numText.setPosition({nx, ny});
        window.draw(numText);
        ny += lineHeight_;
    }

    
    // Вертикальный скроллбар
    const float sbX = layout.editor.pos.x + layout.editor.size.x - vScrollbarW;
    
    // Трек
    sf::RectangleShape track;
    track.setPosition({sbX, layout.editor.pos.y});
    track.setSize({vScrollbarW, layout.editor.size.y - hScrollbarH});
    track.setFillColor(sf::Color(60, 60, 70));
    window.draw(track);

    // Ползунок
    if (totalLines > maxVisible) {
        const float ratio = static_cast<float>(maxVisible) / static_cast<float>(totalLines);
        const float thumbH = std::max(20.f, (layout.editor.size.y - hScrollbarH) * ratio);
        const float maxScroll = static_cast<float>(totalLines - maxVisible);
        const float t = static_cast<float>(startLine) / maxScroll;
        const float thumbY = layout.editor.pos.y + t * ((layout.editor.size.y - hScrollbarH) - thumbH);

        sf::RectangleShape thumb;
        thumb.setPosition({sbX, thumbY});
        thumb.setSize({vScrollbarW, thumbH});
        thumb.setFillColor(sf::Color(180, 180, 200));
        window.draw(thumb);
    }


    // Горизонтальный скроллбар    
    const float hTrackX = layout.editor.pos.x + padding + gutterW;
    const float hTrackY = layout.editor.pos.y + layout.editor.size.y - hScrollbarH;
    const float hTrackW = layout.editor.size.x - vScrollbarW - padding * 2.f - gutterW;
    
    // Трек
    sf::RectangleShape hTrack;
    hTrack.setPosition({hTrackX, hTrackY});
    hTrack.setSize({hTrackW, hScrollbarH});
    hTrack.setFillColor(sf::Color(60, 60, 70));
    window.draw(hTrack);

    // Ползунок (если контент шире viewport)
    if (contentWidth > viewportWidth) {
        const float ratio = viewportWidth / contentWidth;
        const float thumbW = std::max(24.f, hTrackW * ratio);
        const float maxScroll = std::max(1.f, contentWidth - viewportWidth);
        const float t = editorScrollX_ / maxScroll;
        const float thumbX = hTrackX + t * (hTrackW - thumbW);

        sf::RectangleShape hThumb;
        hThumb.setPosition({thumbX, hTrackY});
        hThumb.setSize({thumbW, hScrollbarH});
        hThumb.setFillColor(sf::Color(180, 180, 200));
        window.draw(hThumb);
    }
}


// requestCompile - Запрос компиляции исходного кода
void App::requestCompile() {
    if (!sourceDirty_ && lastCompile_.ok) {
        return;
    }

    Compiler compiler;
    rebuildSourceFromLines();                      // Собираем код из строк
    lastCompile_ = compiler.compile(sourceCode_);  // Компилируем
    sourceDirty_ = false;                          // Код теперь соответствует скомпилированному
    tableScrollX_ = 0.f;                           // Сбрасываем скролл таблицы
    firstVisibleTransitionRow_ = 0;

    if (lastCompile_.ok) {
        mode_ = AppMode::CompiledOk;
        initialTape_ = lastCompile_.initialTape;                    // Сохраняем начальное состояние ленты
        tm_.reset(initialTape_, lastCompile_.table.startState);     // Сбрасываем машину
        tapeOffset_ = tm_.head() - 5;                               // Центрируем ленту на головке
    } else {
        mode_ = AppMode::CompileError;
        std::vector<Diagnostic> diags = lastCompile_.diagnostics;
        for (const auto& diag : diags) {
            std::cout << "Compile Error: " << diag.message << " at line " << diag.line << ", column " << diag.column << std::endl;
        }
        tm_.setHalted(true);
    }
}


// requestResetMachine - Сброс машины в начальное состояние
void App::requestResetMachine() {
    // Нет скомпилированной таблицы
    if (!hasValidTable()) {
        return;
    }
    tm_.reset(initialTape_, lastCompile_.table.startState);
    tapeOffset_ = tm_.head() - 5;
    mode_ = AppMode::ReadyToRun;
}

// requestStep - Выполнение одного шага машины Тьюринга
void App::requestStep() {
    if (!hasValidTable()) {
        return;
    }

    if (tm_.isHalted()) {
        mode_ = AppMode::Halted;
        return;
    }

    // Выполняем один шаг
    const StepResult result = interpreter_.step(tm_, lastCompile_.table);
    
    if (result == StepResult::Ok) {
        // Шаг успешен - сохраняем текущий режим
        if (mode_ == AppMode::Running) {
            mode_ = AppMode::Running;
        } else if (mode_ == AppMode::Paused) {
            mode_ = AppMode::Paused;
        } else {
            mode_ = AppMode::CompiledOk;
        }
        ensureTapeHeadVisible();  // Автоматически прокручиваем к головке
    } else {
        // Машина остановилась (достигла halt state или нет перехода)
        mode_ = AppMode::Halted;
    }
}

// requestRun - Запуск автоматического выполнения
void App::requestRun() {
    if (!hasValidTable()) {
        return;
    }

    if (sourceDirty_) {
        return;
    }

    // Если машина остановлена или только что скомпилирована - сбрасываем
    if (tm_.isHalted() || mode_ == AppMode::CompiledOk) {
        requestResetMachine();
    }

    mode_ = AppMode::Running;
}

// requestPause - Пауза автоматического выполнения
void App::requestPause() {
    if (mode_ == AppMode::Running) {
        mode_ = AppMode::Paused;
    }
}

// requestStop - Полная остановка и сброс
void App::requestStop() {
    if (mode_ == AppMode::Running || mode_ == AppMode::Paused) {
        requestResetMachine();
    }
}

// hasValidTable - Проверка наличия корректной таблицы
bool App::hasValidTable() const {
    return lastCompile_.ok && !sourceDirty_;
}


// markEdited - Пометка кода как изменённого
void App::markEdited() {
    sourceDirty_ = true;
    mode_ = AppMode::IdleEditing;
}