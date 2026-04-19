#pragma once

#include <SFML/Graphics.hpp>
#include <print>
#include <stdexec/execution.hpp>

#include "types_core.hpp"

namespace ex = stdexec;

// Шаблонный класс состояния операции обработки событий SFML-окна
// 1. Создается в connect, когда сендер подключается к ресиверу
// 2. start запускает асинхронную обработку событий SFML-окна
// 3. После обработки вызывает set_value или set_error
// 4. Уничтожается после завершения операции
template <typename Receiver>
class OperationState {
public:
    // Шаблонный конструктор (гарантирует работу с любым типом ресивера)
    // Инициализирует состояние операции
    template <typename R>
    explicit OperationState(R &&r, sf::RenderWindow &window, RenderSettings render_settings, AppState &state)
        : receiver_{std::forward<R>(r)}, window_{window}, render_settings_{render_settings}, state_{state} {}

    // Запускает асинхронную обработку событий SFML-окна
    // (вызывается 1 раз после создания состояния операции)
    void start() noexcept {
        try {
            HandleEvents();
            ex::set_value(std::move(receiver_));  // уведомляем ресивер об успешном завершении
        } catch (...) {
            ex::set_error(std::move(receiver_), std::current_exception());
        }
    }

private:
    // Min интервал между операциями зума, в мс
    static constexpr float ZOOM_INTERVAL_MS = 100.0f;  // регулирует скорость зума при удержании кнопки мыши

    // === Обработчики событий ===
    // Обработчик всех событий SFML-окна
    void HandleEvents() const {
        sf::Event event;

        // === Обработка дискретных событий (запоминаем состояния) ===
        while (window_.pollEvent(event)) {
            switch (event.type) {
            case sf::Event::Closed: {
                state_.should_exit = true;
                break;
            }
            case sf::Event::KeyPressed: {
                HandleKeyPress(event.key);
                break;
            }
            case sf::Event::MouseButtonPressed: {
                HandleMousePress(event.mouseButton);
                break;
            }
            case sf::Event::MouseButtonReleased: {
                HandleMouseRelease(event.mouseButton);
                break;
            }
            default:
                break;
            }
        }

        // === Непрерывная логика (выполняется каждый кадр) ===

        // Обработка удержания кнопок мыши (зум с интервалом)
        if (state_.left_mouse_pressed || state_.right_mouse_pressed) {
            auto mouse_pos = sf::Mouse::getPosition(window_);
            if (mouse_pos.x >= 0 && mouse_pos.x < static_cast<int>(render_settings_.width) && mouse_pos.y >= 0 &&
                mouse_pos.y < static_cast<int>(render_settings_.height)) {

                auto elapsed = state_.zoom_clock.getElapsedTime().asMilliseconds();
                if (elapsed >= ZOOM_INTERVAL_MS) {
                    ZoomToPoint(mouse_pos.x, mouse_pos.y, state_.left_mouse_pressed);
                    state_.zoom_clock.restart();
                }
            }
        }

        HandleAutoZoom();
    }

    // Обработчик нажатий клавиш
    void HandleKeyPress(const sf::Event::KeyEvent &key) const {
        // Используем scancode для независимости от раскладки
        switch (key.scancode) {
            // Переключатель режима автозума к точке "морской конек"
        case sf::Keyboard::Scan::X: {
            std::println("X pressed - toggling auto zoom");
            state_.auto_zoom_enabled = !state_.auto_zoom_enabled;
            state_.need_rerender = true;
            break;
        }
        // Возврат к исходным настройкам отображения
        case sf::Keyboard::Scan::C: {
            std::println("C pressed - resetting viewport");
            state_.viewport = AppState::INITIAL_VIEWPORT;
            state_.auto_zoom_enabled = false;
            state_.need_rerender = true;
            break;
        }
        default:
            break;
        }
    }

    // Обработчик нажатий кнопок мыши (зум)
    void HandleMousePress(const sf::Event::MouseButtonEvent &mouse) const {
        if (mouse.button == sf::Mouse::Left) {
            state_.left_mouse_pressed = true;
            ZoomToPoint(mouse.x, mouse.y, true);
            state_.zoom_clock.restart();  // сбрасываем таймер после зума
        } else if (mouse.button == sf::Mouse::Right) {
            state_.right_mouse_pressed = true;
            ZoomToPoint(mouse.x, mouse.y, false);
            state_.zoom_clock.restart();  // сбрасываем таймер после зума
        }
    }

    // Обработчик отпусканий кнопок мыши
    void HandleMouseRelease(const sf::Event::MouseButtonEvent &mouse) const {
        if (mouse.button == sf::Mouse::Left) {
            state_.left_mouse_pressed = false;
        } else if (mouse.button == sf::Mouse::Right) {
            state_.right_mouse_pressed = false;
        }
    }

    // Обработчик события автозума к точке "морской конек" фрактала
    void HandleAutoZoom() const {
        if (!state_.auto_zoom_enabled) {
            return;
        }

        auto elapsed = state_.zoom_clock.getElapsedTime().asMilliseconds();
        if (elapsed < ZOOM_INTERVAL_MS) {
            return;
        }

        // Нормализованные координаты целевой точки
        const double norm_x = (AppState::AUTO_ZOOM_TARGET_X - state_.viewport.x_min) / state_.viewport.width();
        const double norm_y = (AppState::AUTO_ZOOM_TARGET_Y - state_.viewport.y_min) / state_.viewport.height();

        // Координаты целевого пикселя
        const int pixel_x = static_cast<int>(norm_x * render_settings_.width);
        const int pixel_y = static_cast<int>(norm_y * render_settings_.height);

        // Плавный зум к целевому пикселю
        ZoomToPoint(pixel_x, pixel_y, true);
        state_.zoom_clock.restart();  // сбрасываем таймер после зума
        state_.need_rerender = true;
    }

    // Вычисляет новый вьюпорт при зуме в заданный пиксел
    void ZoomToPoint(int pixel_x, int pixel_y, bool zoom_in, double factor = 0.8) const {
        const double target_x =
            state_.viewport.x_min + (static_cast<double>(pixel_x) / render_settings_.width) * state_.viewport.width();
        const double target_y =
            state_.viewport.y_min + (static_cast<double>(pixel_y) / render_settings_.height) * state_.viewport.height();

        const double zoom_factor = zoom_in ? factor : (1.0 / factor);
        const double new_width = state_.viewport.width() * zoom_factor;
        const double new_height = state_.viewport.height() * zoom_factor;

        // Обновляем viewport с центрированием на целевой точке
        state_.viewport.x_min = target_x - new_width / 2.0;
        state_.viewport.x_max = target_x + new_width / 2.0;
        state_.viewport.y_min = target_y - new_height / 2.0;
        state_.viewport.y_max = target_y + new_height / 2.0;

        state_.need_rerender = true;
    }

    // === Данные состояния операции ===
    Receiver receiver_;               // ресивер (колбэк), который будет вызван при завершении операции
    sf::RenderWindow &window_;        // ссылка на SFML-окно
    RenderSettings render_settings_;  // настройки рендеринга (размеры окна, параметры отрисовки фрактала)
    AppState &state_;                 // ссылка на состояние приложения (вьюпорт, флаги мыши/выхода/перерисовки окна)
};

// Класс сендера асинхронной операции обработки событий SFML
// (создает состояние операции при каждом подключении к ресиверу)
class SfmlEventHandler {
public:
    using sender_concept = ex::sender_t;  // маркер сендера

    // Парамтрический конструктор, инициализирующий сендер ссылками на SFML-ресурсы
    // (ресурсы привязаны к хранилищу SfmlState и живут гарантированно дольше, чем сендер и все состояния операций)
    SfmlEventHandler(sf::RenderWindow &window, RenderSettings render_settings, AppState &state)
        : window_{window}, render_settings_{render_settings}, state_{state} {}

    // Подключает ресивер к сендеру, создавая состояние операции
    // (метод вызывается stdexec, когда сендер готов к выполнению)
    template <typename Receiver>
    auto connect(Receiver &&receiver) const {
        return OperationState<std::decay_t<Receiver>>{std::forward<Receiver>(receiver), window_, render_settings_,
                                                      state_};
    }

    // Описывает сигналы завершения, которые может отправить сендер
    // (вызывается stdexec для проверки совместимости сендера и ресивера)
    template <typename Env>
    auto get_completion_signatures(Env &&) const {
        return ex::completion_signatures<ex::set_value_t(), ex::set_error_t(std::exception_ptr)>{};
    }

private:
    sf::RenderWindow &window_;        // ссылка на SFML-окно
    RenderSettings render_settings_;  // настройки рендеринга (размеры окна, параметры отрисовки фрактала)
    AppState &state_;                 // ссылка на состояние приложения (вьюпорт, флаги мыши/выхода/перерисовки окна)
};
