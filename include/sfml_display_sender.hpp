#pragma once

#include "types_core.hpp"
#include "types_sfml.hpp"

#include <print>
#include <stdexec/execution.hpp>

namespace ex = stdexec;

namespace render {

// Сендер для отрисовки фрактала в SFML-окне
static auto MakeSfmlDisplaySender(SfmlState &st, bool &do_update) {
    static AvrTimeCounter time_counter;
    return ex::then([&st, &do_update](FrameBuffer *fb) {
        if (!do_update) {
            return;
        }

        time_counter.Start();
        st.texture.update(fb->rgba.data());

        st.window.clear();
        st.window.draw(st.sprite);
        st.window.display();
        time_counter.End();

        if (time_counter.Count() % STATS_INTERVAL == 0) {
            std::println("Average display time: {} ms over {} frames", time_counter.GetAvr(), time_counter.Count());
        }
    });
}
}  // namespace render
