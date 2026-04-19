#pragma once

#include "mandelbrot_fractal_utils.hpp"
#include "types_core.hpp"
#include "types_sfml.hpp"

#include <print>
#include <stdexec/execution.hpp>

using namespace std::chrono_literals;
namespace ex = stdexec;

namespace mandelbrot {

// Сендер для расчета фрактала Мандельброта
static auto MakeComputeSender(RenderSettings settings, ViewPort viewport, bool &do_update) {
    static AvrTimeCounter time_counter;

    // Лямбда расчета y-строки изображения фрактала
    auto comp_fractal_raw = [settings, viewport](int y, FrameBuffer *fb) {
        const size_t width = fb->width;
        const size_t height = fb->height;

        // Интерпретируем буфер кадра как массив структур RGBA
        RGBA *pixels = reinterpret_cast<RGBA *>(fb->rgba.data());
        RGBA *row_ptr = pixels + (static_cast<size_t>(y) * width);  // начало текущей строки в буфере кадра

        for (int x = 0; x < fb->width; ++x) {
            auto complex = Pixel2DToComplex(x, y, viewport, width, height);
            auto iters = CalculateIterationsForPoint(complex, settings.max_iterations, settings.escape_radius);
            auto [r, g, b] = IterationsToColor(iters, settings.max_iterations);
            row_ptr[x] = {r, g, b, 0xFF};  // пишем цвет пиксела за одну операцию
        }
    };

    // Примечание:
    // 1. Чтобы реализовать ветвление через ex::let_value, обе ветви должны иметь на выходе одинаковый тип пайпа.
    // 2. Можно реализовать стирание типов через any_sender, но на текущей экспериментальной версии exec добиться
    // компилируемого решения не удается (даже с реализацией фабрики any_sender).
    // 3. В качестве "инженерного" решения предлагается в побочной ветви использовать фиктивный пайп, который полностью
    // повторяет тип рабочего пайпа с bulk и лямбдой, но не запускается на пуле потоков

    return ex::let_value([&do_update, comp_fractal_raw, settings](FrameBuffer *fb) {
               if (do_update) {
                   time_counter.Start();
               }
               // Пайп параллельного расчета фрактала (становится фиктивным при do_update == false)
               return ex::just(fb) | ex::bulk(ex::par, (do_update ? settings.height : 0u), comp_fractal_raw);
           }) |
           ex::then([&do_update](FrameBuffer *fb) {
               if (do_update) {
                   time_counter.End();
                   if (time_counter.Count() % STATS_INTERVAL == 0) {
                       std::println("\nAverage compute time: {} ms over {} frames", time_counter.GetAvr(),
                                    time_counter.Count());
                   }
               }
               return fb;
           });
}

}  // namespace mandelbrot
