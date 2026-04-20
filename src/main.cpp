#include <chrono>
#include <memory>
#include <print>
#include <thread>
#include <utility>

#include <SFML/Graphics.hpp>

#include <exec/any_sender_of.hpp>
#include <exec/repeat_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "mandelbrot_sender.hpp"
#include "sfml_display_sender.hpp"
#include "sfml_events_handler.hpp"
#include "types_sfml.hpp"

using namespace std::chrono_literals;
namespace ex = stdexec;

// Класс-функтор ограничителя частоты синтеза кадров
// (приостанавливает пайп рендеринга, если кадр обработан слишком быстро)
class WaitForFPS {
public:
    static constexpr float TARGET_FPS = 60.0f;                    // целевая частота синтеза кадров, в кадрах/c
    static constexpr float FRAME_TIME_MS = 1000.0f / TARGET_FPS;  // целевое t синтеза одного кадра, в мс

    // Явный констуктор по умолчанию, принимающий измеритель t синтеза кадра
    explicit WaitForFPS(FrameClock &frame_clock, unsigned int target_fps)
        : frame_clock_(frame_clock), frame_time_(1s / target_fps) {}

    // Оператор-функция (вызывается в конце обработки кадра)
    void operator()() {
        auto cur_frame_duration = frame_clock_.GetFrameTime();

        // Если кадр обработан быстрее frame_time_, то приостанавливаем поток рендеринга на разницу времени
        if (cur_frame_duration < frame_time_) {
            std::this_thread::sleep_for(frame_time_ - cur_frame_duration);
        }
        frame_clock_.Reset();  // сбрасываем таймер для следующего кадра
    }

private:
    FrameClock &frame_clock_;  // cсылка на измеритель t синтеза кадра (живет в состоянии операции - SfmlState)
    const std::chrono::milliseconds frame_time_ = 1ms;  // t синтеза одного кадра
};

// Класс приложения для отрисовки фрактала Мандельброта
// (управляет пайпом stdexec и координирует работу всех компонентов)
class MandelbrotApp {
public:
    // Конструктор по умолчанию, инициализирующий пулы потоков
    MandelbrotApp()
        : compute_pool_{std::max(1u, std::thread::hardware_concurrency())},  // число аппаратно-поддерживаемых потоков
          sfml_thread_{1}  // 1 поток для всех операций с окном (требование SFML)
    {
        std::println("hardware_concurrency: {}\n", std::thread::hardware_concurrency());
    }

    // Запускает приложение
    void Run() {
        // Получаем сендеры шедулеров
        auto compute_sched = compute_pool_.get_scheduler();  // пула потоков вычисления фрактала
        auto sfml_sched = sfml_thread_.get_scheduler();      // выделенного потока для работы с SFML

        // Сендер инициализации SFML (создает окно и хранилище ресурсов SFML в выделенном SFML-потоке)
        auto initialize =
            ex::on(sfml_sched,
                   ex::just() | ex::then([this]() {
                       state_ = std::make_unique<SfmlState>(  //
                           RenderSettings{.width = 800, .height = 600, .max_iterations = 100, .escape_radius = 2.0});
                   }));

        // Блокируем основной поток, пока SFML-окно не будет создано
        ex::sync_wait(std::move(initialize));

        // Засекаем t начала синтеза кадра (оно будет обновляться в каждом кадре)
        state_->frame_clock.Reset();

        // Пайп обработки одного кадра визуализации
        auto process_frame =
            // 1. Обрабатываем события SFML-окна в SFML-потоке
            ex::starts_on(sfml_sched, SfmlEventHandler{state_->window, state_->render_settings, state_->app_state}) |
            // 2. Переключаемся на пул потоков и вычисляем фрактал (только при поднятом флаге обновления кадра)
            ex::continues_on(compute_sched) | ex::let_value([this] {
                return ex::just(&state_->fb) |
                       mandelbrot::MakeComputeSender(state_->render_settings, state_->app_state.viewport,
                                                     state_->app_state.need_rerender);
            }) |
            // 3. Переключаемся на SFML-поток и отрисовываем фрактал (только при поднятом флаге обновления кадра)
            ex::continues_on(sfml_sched) | render::MakeSfmlDisplaySender(*state_, state_->app_state.need_rerender) |
            // 4. Усыпляем основной поток, если fps превышает целевое значение
            ex::then([this]() {
                state_->app_state.need_rerender = false;  // опускаем флаг обновления кадра
                WaitForFPS(state_->frame_clock, 60)();
            });

        // Пайп циклической визуализации кадров (останавливается после получения сигнала о закрытии приложения)
        auto repeated_pipeline = std::move(process_frame) | ex::then([this] { return state_->app_state.should_exit; }) |
                                 exec::repeat_until() |
                                 // после сигнала закрытия окна переключаемся на SFML-поток и особождаем ресурсы SFML
                                 ex::continues_on(sfml_sched) | ex::then([this] {
                                     std::println("Cleaning up resources in SFML thread");
                                     if (state_->window.isOpen()) {
                                         state_->window.close();
                                     }
                                     state_.reset();
                                 });

        // Выполняем пайп циклической визуализации кадров, пока пользователь не закроет приложение
        ex::sync_wait(std::move(repeated_pipeline));
    }

private:
    std::unique_ptr<SfmlState> state_;  // хранилище SFML-ресурсов, необходимых для рендеринга кадра

    exec::static_thread_pool compute_pool_;  // пул потоков для параллельных вычислений фрактала
    exec::static_thread_pool sfml_thread_;   // выделенный поток для всех операций с GUI (поток SFML)
};

int main() {
    std::println("=== Mandelbrot Fractal Renderer ===\n");
    std::println("Controls:");
    std::println("  Left Mouse Button  - Zoom In");
    std::println("  Right Mouse Button - Zoom Out");
    std::println("  X                  - Toggle Auto Zoom (infinite zoom to 'Seahorse Valley' point)");
    std::println("  C                  - Reset to Initial View");
    std::println("  Close Window       - Exit\n");

    try {
        MandelbrotApp app;
        app.Run();
    } catch (const std::exception &e) {
        std::println("Error: {}", e.what());
        return 1;
    }
    return 0;
}

// // Тест библиотеки SFML
// int main() {
//     sf::RenderWindow window(sf::VideoMode(800, 600), "SFML Triangle");

//     sf::ConvexShape triangle;
//     triangle.setPointCount(3);
//     triangle.setPoint(0, sf::Vector2f(400.f, 100.f));
//     triangle.setPoint(1, sf::Vector2f(200.f, 500.f));
//     triangle.setPoint(2, sf::Vector2f(600.f, 500.f));
//     triangle.setFillColor(sf::Color::Green);

//     while (window.isOpen()) {
//         sf::Event event;
//         while (window.pollEvent(event)) {
//             if (event.type == sf::Event::Closed)
//                 window.close();
//         }

//         window.clear(sf::Color::Black);
//         window.draw(triangle);
//         window.display();
//     }

//     return 0;
// }