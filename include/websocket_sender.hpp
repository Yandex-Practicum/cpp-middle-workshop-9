/**
 * @file websocket_sender.hpp
 * @brief Sender для WebSocket операций на основе Boost.Beast
 *
 * Обёртка Boost.Beast WebSocket операций в stdexec senders.
 * Реализует P2300 completion signatures:
 *   - set_value() при успешном подключении/чтении
 *   - set_error(std::exception_ptr) при ошибке
 *   - set_stopped() при отмене через stop_token
 */

#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <exec/create.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <memory>
#include <print>
#include <string_view>
#include <thread>

namespace ws {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ex = stdexec;

struct ExecutionContext {
    net::io_context& ioc;
    net::io_context& get() { return ioc; }
};

/**
 * @brief Конфигурация подключения к WebSocket биржи
 */
struct ExchangeConfig {
    std::string_view host;
    std::string_view port;
    std::string_view path;           // WebSocket path (e.g., "/ws")
    bool use_ssl = true;             // WSS vs WS
    std::string_view exchange_name;  //
};

[[nodiscard]]
inline auto connect_sender(ExchangeConfig config, std::reference_wrapper<net::io_context> ioc_ref) -> ex::sender auto {
    return exec::create<
        ex::completion_signatures<ex::set_value_t(std::shared_ptr<websocket::stream<net::ip::tcp::socket>>),
                                  ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>>(
        [config = std::move(config), ioc_ref](auto ctx) noexcept {
            // Создаём io_context для этой операции
            auto& ioc = ioc_ref.get();
            // Создаём resolver
            auto resolver = net::ip::tcp::resolver{ioc};
            // Создаём WebSocket stream
            auto ws = std::make_shared<websocket::stream<net::ip::tcp::socket>>(net::ip::tcp::socket{ioc});

            // Устанавливаем опции WebSocket
            ws->set_option(websocket::stream_base::decorator(
                [](websocket::request_type &req) { req.set(beast::http::field::user_agent, "Terminal/1.0"); }));

            // Проверяем stop_token
            auto stop_token = ex::get_stop_token(ctx);
            if (stop_token.stop_requested()) {
                ex::set_stopped(std::move(ctx.receiver));
                return;
            }

            try {
                // Resolve
                auto endpoints = resolver.resolve(config.host, config.port);
                // Connect
                net::connect(ws->next_layer(), endpoints.begin(), endpoints.end());
                // WebSocket Handshake (no SSL)
                ws->handshake(config.host, config.path);
                // Успешное подключение
                ex::set_value(std::move(ctx.receiver), ws);
            } catch (...) {
                ex::set_error(std::move(ctx.receiver), std::current_exception());
            }
        });
}

[[nodiscard]]
inline auto close_sender(std::shared_ptr<websocket::stream<net::ip::tcp::socket>> ws) -> ex::sender auto {
    return exec::create<
        ex::completion_signatures<ex::set_value_t(), ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>>(
        [ws](auto ctx) noexcept {
            auto stop_token = ex::get_stop_token(ctx);

            // Проверяем stop_token перед началом операции
            if (stop_token.stop_requested()) {
                ex::set_stopped(std::move(ctx.receiver));
                return;
            }

            try {
                // Закрываем WebSocket соединение с кодом нормального закрытия
                ws->close(websocket::close_code::normal);
                // Успешное закрытие
                ex::set_value(std::move(ctx.receiver));
            } catch (const beast::system_error &e) {
                // Игнорируем ошибку, если соединение уже закрыто
                if (e.code() == websocket::error::closed) {
                    ex::set_value(std::move(ctx.receiver));
                    return;
                }
                ex::set_error(std::move(ctx.receiver), std::current_exception());
            } catch (...) {
                ex::set_error(std::move(ctx.receiver), std::current_exception());
            }
        });
}

/**
 * @brief Sender для отправки сообщения подписки (plain TCP)
 *
 * Completion signatures:
 *   - set_value() — успешная отписка
 *   - set_error(std::exception_ptr) — ошибка отправки
 *   - set_stopped() — отмена
 */
[[nodiscard]]

inline auto subscribe_sender(websocket::stream<net::ip::tcp::socket> &ws, std::string_view subscribe_message)
    -> ex::sender auto {
    return exec::create<
        ex::completion_signatures<ex::set_value_t(), ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>>(
        [&ws, subscribe_message](auto ctx) noexcept {
            // Проверяем stop_token перед началом операции
            if (ex::get_stop_token(ctx).stop_requested()) {
                ex::set_stopped(std::move(ctx.receiver));
                return;
            }

            try {
                // Отправляем сообщение подписки
                ws.write(net::buffer(subscribe_message));
                // Успешная отправка
                ex::set_value(std::move(ctx.receiver));
            } catch (...) {
                ex::set_error(std::move(ctx.receiver), std::current_exception());
            }
        });
}

/**
 * @brief Sender для асинхронного чтения сообщения (plain TCP)
 *
 * Completion signatures:
 *   - set_value(beast::flat_buffer) — успешное чтение
 *   - set_error(std::exception_ptr) — ошибка чтения
 *   - set_stopped() — отмена
 */
[[nodiscard]]
inline auto async_read_sender(websocket::stream<net::ip::tcp::socket> &ws) -> ex::sender auto {
    return exec::create<ex::completion_signatures<ex::set_value_t(beast::flat_buffer),
                                                  ex::set_error_t(std::exception_ptr),
                                                  ex::set_stopped_t()>>(
        [&ws](auto ctx) noexcept {
            // Проверяем stop_token перед началом операции
            if (ex::get_stop_token(ctx).stop_requested()) {
                ex::set_stopped(std::move(ctx.receiver));
                return;
            }
            try {
                // Буфер для чтения
                auto buffer = beast::flat_buffer{};
                // Читаем сообщение
                ws.read(buffer);
                // Успешное чтение
                ex::set_value(std::move(ctx.receiver), std::move(buffer));

            } catch (const beast::system_error &e) {
                // Проверяем на закрытие соединения
                if (e.code() == websocket::error::closed) {
                    // WebSocket закрыт удалённой стороной — это не ошибка
                    ex::set_stopped(std::move(ctx.receiver));
                    return;
                }
                ex::set_error(std::move(ctx.receiver), std::current_exception());
            } catch (...) {
                ex::set_error(std::move(ctx.receiver), std::current_exception());
            }
        });
}

/**
 * @brief Sender для печати полученного сообщения на экран
 *
 * Completion signatures:
 *   - set_value() — успешная печать
 *   - set_error(std::exception_ptr) — ошибка печати
 *   - set_stopped() — отмена
 * @note для конвертации beast::flat_buffer в string используйте beast::buffers_to_string(buffer.data())
 */
[[nodiscard]]
inline auto print_message_sender(beast::flat_buffer buffer, std::string_view prefix) -> ex::sender auto{
    return exec::create<
        ex::completion_signatures<ex::set_value_t(), ex::set_error_t(std::exception_ptr), ex::set_stopped_t()>>(
        [buf = std::move(buffer), prefix](auto ctx) noexcept {
            try {
                // Конвертируем buffer в string и печатаем
                auto data = beast::buffers_to_string(buf.data());
                std::println("[ThreadId: {}] {}{}", std::this_thread::get_id(), prefix, data);
                // Успешная печать
                ex::set_value(std::move(ctx.receiver));
            } catch (...) {
                ex::set_error(std::move(ctx.receiver), std::current_exception());
            }
        });
}

}  // namespace ws
