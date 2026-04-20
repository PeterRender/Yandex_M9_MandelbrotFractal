#include <gtest/gtest.h>

#include <SFML/Graphics.hpp>
#include <exec/static_thread_pool.hpp>
#include <queue>

#include "sfml_events_handler.hpp"
#include "test_receiver.hpp"

// Фикстура для тестов сендера с настроенными исходными данными
class SfmlSenderTest : public ::testing::Test {
protected:
    // Исходные данные (должны жить дольше, чем пул потоков)
    RenderSettings settings_{.width = 800, .height = 600};
    AppState state_;
    sf::RenderWindow window_;

    // Выделенный поток для обработки GUI (объявляем после данных, чтобы уничтожался первым)
    exec::static_thread_pool sfml_thread_{1};
};

// Тест успешного открытия окна
TEST_F(SfmlSenderTest, SuccessStartWindow) {
    TestReceiver<> receiver;
    auto state_handle = receiver;               // копия состояния ресивера (через shared_ptr)
    auto sched = sfml_thread_.get_scheduler();  // получаем шедулер

    // Тестовый пайп создания окна, обработки событий и закрытия
    auto wnd_pipe = ex::just() | ex::let_value([this]() {
                        window_.create(sf::VideoMode{settings_.width, settings_.height}, "Test Window");
                        return SfmlEventHandler{window_, settings_, state_};
                    }) |
                    ex::then([this]() {
                        if (window_.isOpen()) {
                            window_.close();
                        }
                    });

    // Собираем тестовый пайп на шедулере выделенного потока
    auto sender = ex::starts_on(sched, std::move(wnd_pipe));

    // Подключаем сендер к тестовому ресиверу и запускаем операцию
    auto op = ex::connect(std::move(sender), std::move(receiver));
    ex::start(op);

    // Ждем завершения операции (через копию shared_ptr)
    state_handle.wait();

    // Проверка сигналов
    EXPECT_TRUE(state_handle.has_value());         // есть сигнал успеха
    EXPECT_FALSE(state_.should_exit);              // событие закрытия окна не должно быть перехвачено
    EXPECT_FALSE(state_handle.was_stopped());      // нет сигнала прерывания
    EXPECT_EQ(state_handle.get_error(), nullptr);  // нет ошибок
}

// Прокси-класс для доступа к защищенной логике OperationState
template <typename Receiver>
class OperationStateProxy : public OperationState<Receiver> {
public:
    using OperationState<Receiver>::OperationState;

    // Прокси-методы для вызова private-обработчиков
    void ProxyHandleKeyPress(const sf::Event::KeyEvent &key) { this->HandleKeyPress(key); }
    void ProxyHandleMouseWheel(const sf::Event::MouseWheelScrollEvent &wheel) { this->HandleMouseWheel(wheel); }
    void ProxyHandleMousePress(const sf::Event::MouseButtonEvent &mouse) { this->HandleMousePress(mouse); }
    void ProxyHandleMouseMove(const sf::Event::MouseMoveEvent &move) { this->HandleMouseMove(move); }
};

// Тест переключения автозума (Клавиша X)
TEST_F(SfmlSenderTest, ToggleAutoZoom) {
    TestReceiver<> receiver;
    OperationStateProxy<TestReceiver<>> op_state{receiver, window_, settings_, state_};

    sf::Event::KeyEvent key;
    key.scancode = sf::Keyboard::Scan::X;

    op_state.ProxyHandleKeyPress(key);
    EXPECT_TRUE(state_.auto_zoom_enabled);  // первое нажатие включает
    EXPECT_TRUE(state_.need_rerender);

    op_state.ProxyHandleKeyPress(key);  // второе - отключает
    EXPECT_FALSE(state_.auto_zoom_enabled);
}

// Тест сброса настроек отображения (Клавиша C)
TEST_F(SfmlSenderTest, ResetViewport) {
    // Имитируем изменения в процессе визуализации
    state_.auto_zoom_enabled = true;
    state_.viewport.x_min = 10.0;
    state_.need_rerender = false;

    TestReceiver<> receiver;
    OperationStateProxy<TestReceiver<>> op_state{receiver, window_, settings_, state_};

    sf::Event::KeyEvent key;
    key.scancode = sf::Keyboard::Scan::C;

    op_state.ProxyHandleKeyPress(key);
    EXPECT_FALSE(state_.auto_zoom_enabled);
    EXPECT_DOUBLE_EQ(state_.viewport.x_min, AppState::INITIAL_VIEWPORT.x_min);
    EXPECT_TRUE(state_.need_rerender);
}

// Тест автоматического отключения автозума при клике
TEST_F(SfmlSenderTest, DisableAutoZoomByClick) {
    state_.auto_zoom_enabled = true;  // включаем автозум

    TestReceiver<> receiver;
    OperationStateProxy<TestReceiver<>> op_state{receiver, window_, settings_, state_};

    // Любой клик мышью должен отключать автозум для ручного управления
    sf::Event::MouseButtonEvent mouse;
    mouse.button = sf::Mouse::Left;
    op_state.ProxyHandleMousePress(mouse);

    EXPECT_FALSE(state_.auto_zoom_enabled);
    EXPECT_TRUE(state_.left_mouse_pressed);
}

// Тест масштабирования вьюпорта кликами мыши (Zoom In / Zoom Out)
TEST_F(SfmlSenderTest, ZoomInOut) {
    TestReceiver<> receiver;
    OperationStateProxy<TestReceiver<>> op_state{receiver, window_, settings_, state_};

    // Фиксируем исходную ширину области вывода
    double start_width = state_.viewport.x_max - state_.viewport.x_min;
    state_.need_rerender = false;

    // Имитируем клик левой кнопкой (Zoom In)
    sf::Event::MouseButtonEvent left_click;
    left_click.button = sf::Mouse::Left;
    left_click.x = 400;
    left_click.y = 300;

    op_state.ProxyHandleMousePress(left_click);

    double zoomed_in_width = state_.viewport.x_max - state_.viewport.x_min;
    EXPECT_LT(zoomed_in_width, start_width);  // область должна сузиться
    EXPECT_TRUE(state_.need_rerender);

    state_.need_rerender = false;

    // Имитируем клик правой кнопкой (Zoom Out)
    sf::Event::MouseButtonEvent right_click;
    right_click.button = sf::Mouse::Right;
    right_click.x = 400;
    right_click.y = 300;

    op_state.ProxyHandleMousePress(right_click);

    double zoomed_out_width = state_.viewport.x_max - state_.viewport.x_min;
    EXPECT_GT(zoomed_out_width, zoomed_in_width);  // область должна расшириться
    EXPECT_TRUE(state_.need_rerender);
}