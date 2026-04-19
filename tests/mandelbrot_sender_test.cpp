#include <gtest/gtest.h>

#include <algorithm>
#include <exec/static_thread_pool.hpp>
#include <ranges>

#include "mandelbrot_sender.hpp"
#include "test_receiver.hpp"
#include "types_core.hpp"

using namespace mandelbrot;

// Фикстура для теста сендера с настроенными исходными данными
class MandelbrotSenderTest : public ::testing::Test {
protected:
    // Исходные данные (должны жить дольше, чем пул потоков)
    RenderSettings settings_{.width = 16, .height = 16, .max_iterations = 100};
    ViewPort viewport_{.x_min = -2.5, .x_max = 1.5, .y_min = -2.0, .y_max = 2.0};
    FrameBuffer fb_ = FrameBuffer::Make(16, 16);  // фабричный метод для инициализации буфера кадра
    bool do_update_ = false;

    // Пул потоков (объявляем после данных, чтобы уничтожался первым)
    exec::static_thread_pool compute_pool_{4};

    void SetUp() override {
        // Очищаем буфер черным цветом с альфой = 0
        std::fill(fb_.rgba.begin(), fb_.rgba.end(), uint8_t{0});
    }
};

// Тест выполнения расчета фрактала при do_update == true
TEST_F(MandelbrotSenderTest, ComputeFractalNormal) {
    do_update_ = true;

    TestReceiver<FrameBuffer *> receiver;
    auto state_handle = receiver;                // копия состояния ресивера (через shared_ptr)
    auto sched = compute_pool_.get_scheduler();  // получаем шедулер

    // Собираем тестовый пайп на шедулере пула
    auto sender = ex::starts_on(sched, ex::just(&fb_)) | MakeComputeSender(settings_, viewport_, do_update_);

    // Подключаем сендер к тестовому ресиверу и запускаем операцию
    auto op = ex::connect(std::move(sender), std::move(receiver));
    ex::start(op);

    // Ждем завершения операции (через копию shared_ptr)
    state_handle.wait();

    // Проверяем, что ресивер получил успешный результат - буфер кадра
    ASSERT_TRUE(state_handle.has_value());
    EXPECT_EQ(std::get<0>(state_handle.get_value()), &fb_);

    // // Проверяем, что в буфере кадра хотя бы один пиксель стал непрозрачным
    // bool has_alpha = std::any_of(fb_.rgba.begin(), fb_.rgba.end(), [](uint8_t val) { return val == 0xFF; });
    // EXPECT_TRUE(has_alpha);

    // Создаем отображение на пиксели буфера кадра
    RGBA *pixels = reinterpret_cast<RGBA *>(fb_.rgba.data());
    size_t pixel_count = fb_.width * fb_.height;
    auto pixel_range = std::views::counted(pixels, pixel_count);

    // Проверяем, что все альфы стали непрозрачными
    auto alpha_is_opaque = [](const RGBA &p) { return p.a == OPAQUE; };
    EXPECT_TRUE(std::ranges::all_of(pixel_range, alpha_is_opaque)) << "Not all alpha channels are opaque";

    // Проверяем, что фрактал раскрасился (не все изображение черное)
    auto is_not_black = [](const RGBA &p) { return p.r != 0 || p.g != 0 || p.b != 0; };
    EXPECT_TRUE(std::ranges::any_of(pixel_range, is_not_black)) << "All pixels are black - fractal wasn't computed";
}

// Тест пропуска расчета фрактала при do_update == false (проверяем работу фиктивного bulk)
TEST_F(MandelbrotSenderTest, SkipFractalComputation) {
    do_update_ = false;

    TestReceiver<FrameBuffer *> receiver;
    auto state_handle = receiver;                // копия состояния ресивера (через shared_ptr)
    auto sched = compute_pool_.get_scheduler();  // получаем шедулер

    // Собираем тестовый пайп на шедулере пула
    auto sender = ex::starts_on(sched, ex::just(&fb_)) | MakeComputeSender(settings_, viewport_, do_update_);

    // Подключаем сендер к тестовому ресиверу и запускаем операцию
    auto op = ex::connect(std::move(sender), std::move(receiver));
    ex::start(op);

    // Ждем завершения операции (через копию shared_ptr)
    state_handle.wait();

    // Проверяем, что ресивер получил успешный результат
    ASSERT_TRUE(state_handle.has_value());

    // Проверяем, что буфер кадра остался черным
    for (auto byte : fb_.rgba) {
        ASSERT_EQ(byte, 0);
    }
}

// Тест проброса исключения через пайплайн расчета фрактала
TEST_F(MandelbrotSenderTest, PassExceptionThroughPipe) {
    do_update_ = true;

    TestReceiver<FrameBuffer *> receiver;
    auto state_handle = receiver;                // копия состояния ресивера (через shared_ptr)
    auto sched = compute_pool_.get_scheduler();  // получаем шедулер

    // Создаем сендер, который гарантированно бросает исключение перед расчетом фрактала
    auto sender = ex::starts_on(sched, ex::just(&fb_)) | ex::then([](FrameBuffer *fb) -> FrameBuffer * {
                      throw std::runtime_error("Compute error");
                      return fb;
                  }) |
                  MakeComputeSender(settings_, viewport_, do_update_);

    // Подключаем сендер к тестовому ресиверу и запускаем операцию
    auto op = ex::connect(std::move(sender), std::move(receiver));
    ex::start(op);

    // Ждем завершения операции (через копию shared_ptr)
    state_handle.wait();

    // Проверяем, что канал со значеним пуст, а канал ошибки содержит наше исключение
    EXPECT_FALSE(state_handle.has_value());
    EXPECT_NE(state_handle.get_error(), nullptr);
    try {
        if (state_handle.get_error())
            std::rethrow_exception(state_handle.get_error());
    } catch (const std::runtime_error &e) {
        EXPECT_STREQ(e.what(), "Compute error");
    }
}