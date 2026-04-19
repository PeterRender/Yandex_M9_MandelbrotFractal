#pragma once

#include <SFML/Config.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>

#include <cstdint>
#include <vector>

#include "types_core.hpp"

// Буфер кадра в RAM
struct FrameBuffer {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<sf::Uint8> rgba;

    static FrameBuffer Make(std::uint32_t w, std::uint32_t h) {
        FrameBuffer fb;
        fb.width = w;
        fb.height = h;
        fb.rgba.resize(static_cast<size_t>(w) * h * 4u);
        return fb;
    }
};

// Хранилище SFML-ресурсов, необходимых для рендеринга кадра
// (должно создаваться и использоваться только в SFML-потоке)
struct SfmlState {
    RenderSettings render_settings;  // настройки рендеринга (размеры окна, параметры отрисовки фрактала)
    AppState app_state;              // состояние приложения (вьюпорт, флаги мыши/выхода/перерисовки окна)

    // Графические ресурсы SFML
    sf::RenderWindow window;  // SFML-окно
    sf::Texture texture;      // текстура в VRAM
    sf::Sprite sprite;        // спрайт для отрисовки посчитанной текстуры
    FrameBuffer fb;           // буфер кадра в RAM

    FrameClock frame_clock;  // таймер для расчета t синтеза кадра (используется в WaitForFPS)

    // Создает окно и инициализирует графические ресурсы SFML
    explicit SfmlState(const RenderSettings &s)
        : render_settings(s),
          window(sf::VideoMode{render_settings.width, render_settings.height}, "Mandelbrot Fractal"),
          fb(FrameBuffer::Make(render_settings.width, render_settings.height)) {

        // Отключаем повторение клавиш (одно событие при нажатии клавиши)
        window.setKeyRepeatEnabled(false);

        // Создаем текстуру размером с окно (в VRAM)
        texture.create(render_settings.width, render_settings.height);

        // Привязываем текстуру к спрайту
        sprite.setTexture(texture);
    }
};
