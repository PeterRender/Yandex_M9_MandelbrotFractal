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
    return ex::then([settings, viewport, &do_update](FrameBuffer *fb) {
               if (!do_update) {
                   return fb;
               }

               time_counter.Start();

               // TODO: добавить расчет пикселов фрактала

               return fb;
           }) |
           ex::then([](FrameBuffer *fb) {
               time_counter.End();
               if (time_counter.Count() % STATS_INTERVAL == 0) {
                   std::println("\nAverage compute time: {} ms over {} frames", time_counter.GetAvr(),
                                time_counter.Count());
               }
               return fb;
           });
}

}  // namespace mandelbrot
