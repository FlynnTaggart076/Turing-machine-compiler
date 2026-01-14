#pragma once

#include <string>
#include <vector>

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Text.hpp>

#include "Compiler.h"
#include "Interpreter.h"
#include "TuringMachine.h"

enum class AppMode {
    IdleEditing,
    CompiledOk,
    CompileError,
    ReadyToRun,
    Running,
    Paused,
    Halted
};

class App {
public:
    App();

    /**
     * @brief Обработать событие SFML (ввод пользователя)
     * @param event Событие (нажатие клавиши, клик мыши и т.д.)
     * @param window Окно для закрытия при необходимости
     */
    void handleEvent(const sf::Event& event, sf::RenderWindow& window);

    /**
     * @brief Обновить состояние приложения
     * @param dt Время с предыдущего кадра в секундах
     * 
     * В режиме Running автоматически выполняет шаги машины.
     */
    void update(float dt);

    /**
     * @brief Отрисовать весь интерфейс
     * @param window Окно для отрисовки
     */
    void render(sf::RenderWindow& window);

    // ============================================================
    // Команды пользователя (могут вызываться по клавишам или кнопкам)
    // ============================================================

    /** @brief Скомпилировать исходный код в таблицу переходов */
    void requestCompile();

    /** @brief Сбросить машину в начальное состояние */
    void requestResetMachine();

    /** @brief Выполнить один шаг машины Тьюринга */
    void requestStep();

    /** @brief Запустить автоматическое выполнение */
    void requestRun();

    /** @brief Приостановить автоматическое выполнение */
    void requestPause();

    /** @brief Остановить выполнение и сбросить машину */
    void requestStop();

private:
    // ============================================================
    // Вспомогательные структуры для layout
    // ============================================================

    /** @brief Прямоугольная область экрана */
    struct Region {
        sf::Vector2f pos;
        sf::Vector2f size;
    };

    /** @brief Раскладка всех областей интерфейса */
    struct Layout {
        Region editor;
        Region tape;
        Region controls;
        Region table;
    };

    /** @brief Описание кнопки управления */
    struct ControlButtonSpec {
        sf::FloatRect rect;
        std::string label;
        bool enabled{true};
    };

    // ============================================================
    // Методы отрисовки компонентов
    // ============================================================

    /** @brief Вычислить раскладку на основе размера окна */
    Layout computeLayout(const sf::Vector2u& size) const;

    /** @brief Отрисовать редактор исходного кода */
    void renderEditor(sf::RenderWindow& window, const Layout& layout);

    /** @brief Отрисовать визуализацию ленты */
    void renderTape(sf::RenderWindow& window, const Layout& layout);

    /** @brief Отрисовать панель управления с кнопками */
    void renderControls(sf::RenderWindow& window, const Layout& layout);

    /** @brief Отрисовать таблицу переходов */
    void renderTable(sf::RenderWindow& window, const Layout& layout);

    // ============================================================
    // Обработка ввода
    // ============================================================

    /** @brief Обработать ввод текста в редактор */
    void handleEditorText(char32_t unicode);

    /** @brief Обработать специальные клавиши в редакторе */
    void handleEditorKey(const sf::Event::KeyPressed& key);

    /** @brief Обработать клик по панели управления */
    void handleControlClick(const sf::Vector2f& pos, const Layout& layout);

    /** @brief Построить список кнопок управления */
    std::vector<ControlButtonSpec> buildControlButtons(const Layout& layout) const;

    // ============================================================
    // Вспомогательные методы
    // ============================================================

    /** @brief Пересобрать sourceCode_ из строк редактора */
    void rebuildSourceFromLines();

    /** @brief Ограничить курсор в пределах текста */
    void clampCursor();

    /** @brief Прокрутить ленту на заданное число ячеек */
    void scrollTape(int deltaCells);

    /** @brief Убедиться, что головка видна на экране */
    void ensureTapeHeadVisible();

    /** @brief Ограничить смещение ленты в пределах содержимого */
    void clampTapeOffsetToContent(std::size_t visibleCells);

    /** @brief Ограничить горизонтальную прокрутку таблицы */
    void clampTableScroll(float viewportWidth, std::size_t columns);

    /** @brief Ограничить горизонтальную прокрутку редактора */
    void clampEditorScroll(float viewportWidth, float contentWidth);

    /** @brief Проверить, есть ли валидная таблица переходов */
    bool hasValidTable() const;

    /** @brief Пометить исходный код как изменённый */
    void markEdited();

    // ============================================================
    // Состояние приложения
    // ============================================================

    std::string sourceCode_;
    std::vector<std::string> editorLines_{1};
    std::size_t cursorRow_{0};
    std::size_t cursorCol_{0};
    std::size_t firstVisibleLine_{0};
    float editorScrollX_{0.f};
    float editorVScrollbarWidth_{8.f};
    float editorHScrollbarHeight_{8.f};
    float editorLineNumberWidth_{48.f};

    sf::Font font_;                       
    bool fontLoaded_{false};
    float lineHeight_{18.f};

    bool sourceDirty_{true};              
    CompileResult lastCompile_{};         
    TuringMachine tm_{};                 
    Interpreter interpreter_{};           
    AppMode mode_{AppMode::IdleEditing};  
    Tape initialTape_{};                  

    // Параметры отображения ленты
    long long tapeOffset_{-5};            
    std::size_t tapeVisibleCells_{20};    
    float tapeCellWidth_{96.f};           

    float tapeCellHeight_{80.f};          
    float tapePadding_{8.f};              

    // Параметры отображения таблицы переходов
    std::size_t firstVisibleTransitionRow_{0}; 
    float tableRowHeight_{24.f};          
    float tableScrollX_{0.f};             
    float tableColWidth_{180.f};          
};
