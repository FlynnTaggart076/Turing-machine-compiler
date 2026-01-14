#include <optional>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

#include "App.h"

static void setupConsoleUtf8() {
#ifdef _WIN32
    // Если консоль не подключена (GUI-запуск), ничего не делать.
    if (GetConsoleWindow() == nullptr) {
        return;
    }

    // Диагностика хранится в виде строк UTF-8; устанавливаем соответствующие кодовые страницы консоли.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

int main() {
    setupConsoleUtf8();

    const auto desktop = sf::VideoMode::getDesktopMode();
    
    // Создание полноэкранного окна для максимального использования пространства
    sf::RenderWindow window(desktop, "Turing Machine", sf::Style::Default, sf::State::Fullscreen);
    
    window.setFramerateLimit(60);

    App app;
    
    sf::Clock clock;

    // Главный цикл
    while (window.isOpen()) {
        while (const std::optional event = window.pollEvent()) {
            app.handleEvent(*event, window);
        }

        const float dt = clock.restart().asSeconds();
        app.update(dt);

        app.render(window);
    }

    return 0;
}
