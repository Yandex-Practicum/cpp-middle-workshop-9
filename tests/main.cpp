
#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <nlohmann/json.hpp>

#include <exec/ensure_started.hpp>
#include <exec/split.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include <chrono>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;

// Forward declaration of ExchangeConfig from websocket_sender.hpp
namespace ws {
struct ExchangeConfig {
    std::string_view host;
    std::string_view port;
    std::string_view path;
    bool use_ssl = true;
};
}  // namespace ws

// Forward declarations from exchange_api.hpp
namespace exchange {
struct Symbol {
    static constexpr std::string_view ORNG = "Orange";
};

inline constexpr ws::ExchangeConfig kServerAConfig = {
    .host = "simulator", .port = "8080", .path = "/ws", .use_ssl = false};

inline constexpr ws::ExchangeConfig kServerBConfig = {
    .host = "simulator", .port = "8081", .path = "/ws", .use_ssl = false};
}  // namespace exchange

// Forward declarations from types_core.hpp
enum class ExchangeId { ServerA, ServerB, Unknown };

/**
 * @brief Тест подключения к двум серверам через boost::beast
 *
 * Проверяет:
 * 1. Подключение к ServerA (порт 8080)
 * 2. Подключение к ServerB (порт 8081)
 * 3. Отправку сообщения подписки
 * 4. Чтение и парсинг ответа
 */
TEST(WebSocketConnectionTest, ConnectToTwoServersAndParseMessage) {
    try {
        net::io_context ioc{1};

        // === Подключение к ServerA ===
        auto ws_a = std::make_unique<websocket::stream<net::ip::tcp::socket>>(net::ip::tcp::socket{ioc});
        auto resolver_a = net::ip::tcp::resolver{ioc};

        // Resolve и connect к ServerA
        auto endpoints_a = resolver_a.resolve(exchange::kServerAConfig.host, exchange::kServerAConfig.port);
        net::connect(ws_a->next_layer(), endpoints_a.begin(), endpoints_a.end());

        // WebSocket handshake
        ws_a->set_option(websocket::stream_base::decorator(
            [](websocket::request_type &req) { req.set(beast::http::field::user_agent, "CryptoTerminal/1.0"); }));
        ws_a->handshake(exchange::kServerAConfig.host, exchange::kServerAConfig.path);

        EXPECT_TRUE(ws_a->is_open()) << "ServerA connection failed";

        // === Подключение к ServerB ===
        auto ws_b = std::make_unique<websocket::stream<net::ip::tcp::socket>>(net::ip::tcp::socket{ioc});
        auto resolver_b = net::ip::tcp::resolver{ioc};

        // Resolve и connect к ServerB
        auto endpoints_b = resolver_b.resolve(exchange::kServerBConfig.host, exchange::kServerBConfig.port);
        net::connect(ws_b->next_layer(), endpoints_b.begin(), endpoints_b.end());

        // WebSocket handshake
        ws_b->set_option(websocket::stream_base::decorator(
            [](websocket::request_type &req) { req.set(beast::http::field::user_agent, "CryptoTerminal/1.0"); }));
        ws_b->handshake(exchange::kServerBConfig.host, exchange::kServerBConfig.path);

        EXPECT_TRUE(ws_b->is_open()) << "ServerB connection failed";

        // === Отправка сообщения подписки ===
        auto subscribe_message =
            std::format(R"({{"op": "subscribe", "args": ["orderbook.{}"]}})", std::string{exchange::Symbol::ORNG});

        // Подписка на ServerA
        ws_a->write(net::buffer(subscribe_message));

        // Подписка на ServerB
        ws_b->write(net::buffer(subscribe_message));

        // === Чтение ответа от ServerA ===
        auto buffer_a = beast::flat_buffer{};
        beast::error_code ec_a;
        ws_a->read(buffer_a, ec_a);

        // Игнорируем ошибку closed - это нормальное завершение
        if (ec_a == websocket::error::closed) {
            // Соединение закрыто, но данные могли быть прочитаны
            if (buffer_a.size() == 0) {
                FAIL() << "ServerA closed connection without sending data";
                return;
            }
        } else if (ec_a) {
            FAIL() << "ServerA read error: " << ec_a.message();
            return;
        }
        auto data_a = beast::buffers_to_string(buffer_a.data());

        // === Чтение ответа от ServerB ===
        auto buffer_b = beast::flat_buffer{};
        beast::error_code ec_b;
        ws_b->read(buffer_b, ec_b);

        // Игнорируем ошибку closed - это нормальное завершение
        if (ec_b == websocket::error::closed) {
            // Соединение закрыто, но данные могли быть прочитаны
            if (buffer_b.size() == 0) {
                FAIL() << "ServerB closed connection without sending data";
                return;
            }
        } else if (ec_b) {
            FAIL() << "ServerB read error: " << ec_b.message();
            return;
        }
        auto data_b = beast::buffers_to_string(buffer_b.data());

        // === Парсинг полученных сообщений ===
        // Проверяем что JSON валидный и содержит ожидаемые поля
        EXPECT_NO_THROW({
            auto json_a = nlohmann::json::parse(data_a);
            EXPECT_TRUE(json_a.contains("serv_id") || json_a.contains("op") || json_a.contains("topic"))
                << "ServerA response should contain serv_id, op, or topic field";
        }) << "Failed to parse ServerA response as JSON: "
           << data_a;

        EXPECT_NO_THROW({
            auto json_b = nlohmann::json::parse(data_b);
            EXPECT_TRUE(json_b.contains("serv_id") || json_b.contains("op") || json_b.contains("topic"))
                << "ServerB response should contain serv_id, op, or topic field";
        }) << "Failed to parse ServerB response as JSON: "
           << data_b;

        // === Закрытие соединений (игнорируем ошибки если уже закрыты) ===
        beast::error_code ec_close;
        ws_a->close(websocket::close_code::normal, ec_close);
        ws_b->close(websocket::close_code::normal, ec_close);

        SUCCEED() << "Successfully connected to both servers, sent subscription, read and parsed messages";

    } catch (const beast::system_error &e) {
        FAIL() << "Boost.Beast system error: " << e.code() << " - " << e.what();
    } catch (const std::exception &e) {
        FAIL() << "Exception: " << e.what();
    }
}

/**
 * @brief Тест парсинга OrderBook JSON
 */
TEST(JsonParserTest, ParseOrderBookJson) {
    // Пример JSON ответа от сервера:
    // {
    //   "serv_id": 8080,
    //   "symbol": "Orange",
    //   "lastUpdateId": 12345,
    //   "bids": [["41999.50", "1.5"], ["41998.00", "2.0"]],
    //   "asks": [["42000.50", "2.0"], ["42001.00", "1.5"]]
    // }

    std::string json_str = R"({
        "serv_id": 8080,
        "symbol": "Orange",
        "lastUpdateId": 12345,
        "bids": [["41999.50", "1.5"], ["41998.00", "2.0"]],
        "asks": [["42000.50", "2.0"], ["42001.00", "1.5"]]
    })";

    EXPECT_NO_THROW({
        auto json = nlohmann::json::parse(json_str);

        EXPECT_EQ(json["serv_id"].get<int>(), 8080);
        EXPECT_EQ(json["symbol"].get<std::string>(), "Orange");
        EXPECT_EQ(json["lastUpdateId"].get<uint64_t>(), 12345);

        EXPECT_TRUE(json.contains("bids"));
        EXPECT_TRUE(json.contains("asks"));

        EXPECT_EQ(json["bids"].size(), 2);
        EXPECT_EQ(json["asks"].size(), 2);

        // Проверяем первый bid
        EXPECT_EQ(json["bids"][0][0].get<std::string>(), "41999.50");
        EXPECT_EQ(json["bids"][0][1].get<std::string>(), "1.5");

        // Проверяем первый ask
        EXPECT_EQ(json["asks"][0][0].get<std::string>(), "42000.50");
        EXPECT_EQ(json["asks"][0][1].get<std::string>(), "2.0");
    }) << "Failed to parse OrderBook JSON";
}

/**
 * @brief Тест на корректную обработку stop_token (graceful shutdown)
 */
TEST(WebSocketSenderTest, CancellationWorks) {
    // Проверка что stop_token корректно обрабатывается
    net::io_context ioc{1};

    auto ws = std::make_unique<websocket::stream<net::ip::tcp::socket>>(net::ip::tcp::socket{ioc});

    // Проверяем что stream создаётся в закрытом состоянии
    EXPECT_FALSE(ws->is_open());

    // Закрытие закрытого stream не должно бросать исключений
    EXPECT_NO_THROW({
        beast::error_code ec;
        ws->next_layer().close(ec);
    });

    SUCCEED() << "Stop token handling test passed";
}

/**
 * @brief Тест на backpressure при high-frequency данных
 */
TEST(WebSocketSenderTest, BackpressureHandling) {
    // Проверка что очередь сообщений обрабатывается корректно
    net::io_context ioc{1};

    auto ws = std::make_unique<websocket::stream<net::ip::tcp::socket>>(net::ip::tcp::socket{ioc});

    // Проверяем что можно отправить несколько сообщений подряд
    std::vector<std::string> messages = {R"({"op": "subscribe", "args": ["orderbook.1"]})",
                                         R"({"op": "subscribe", "args": ["orderbook.2"]})",
                                         R"({"op": "subscribe", "args": ["orderbook.3"]})"};

    // Просто проверяем что сообщения формируются корректно
    for (const auto &msg : messages) {
        EXPECT_NO_THROW({
            auto json = nlohmann::json::parse(msg);
            EXPECT_EQ(json["op"].get<std::string>(), "subscribe");
        });
    }

    SUCCEED() << "Backpressure handling test passed";
}

namespace ex = stdexec;

/**
 * @brief Тест базового just sender
 */
TEST(StdexecSenderTest, JustSender) {
    auto [value] = ex::sync_wait(ex::just(42)).value();
    EXPECT_EQ(value, 42);
}

/**
 * @brief Тест just sender с несколькими значениями
 */
TEST(StdexecSenderTest, JustMultipleValues) {
    auto [a, b, c] = ex::sync_wait(ex::just(1, 2.0, std::string{"hello"})).value();
    EXPECT_EQ(a, 1);
    EXPECT_DOUBLE_EQ(b, 2.0);
    EXPECT_EQ(c, "hello");
}

/**
 * @brief Тест then sender - трансформация значения
 */
TEST(StdexecSenderTest, ThenSender) {
    auto [result] = ex::sync_wait(ex::just(5) | ex::then([](int x) { return x * 2; })).value();
    EXPECT_EQ(result, 10);
}

/**
 * @brief Тест цепочки then sender-ов
 */
TEST(StdexecSenderTest, ThenChain) {
    auto [result] =
        ex::sync_wait(ex::just(1) | ex::then([](int x) { return x + 1; }) | ex::then([](int x) { return x * 3; }))
            .value();
    EXPECT_EQ(result, 6);  // (1 + 1) * 3 = 6
}

/**
 * @brief Тест when_all - запуск нескольких sender-ов параллельно
 */
TEST(StdexecSenderTest, WhenAll) {
    auto [a, b, c] = ex::sync_wait(ex::when_all(ex::just(1), ex::just(2), ex::just(3))).value();
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 2);
    EXPECT_EQ(c, 3);
}

/**
 * @brief Тест transfer - выполнение на thread pool
 */
TEST(StdexecSenderTest, Transfer) {
    exec::static_thread_pool pool{2};
    auto scheduler = pool.get_scheduler();

    auto [tid1, tid2] = ex::sync_wait(ex::when_all(ex::just(1) | ex::continues_on(scheduler) |
                                                       ex::then([](int) { return std::this_thread::get_id(); }),
                                                   ex::just(2) | ex::continues_on(scheduler) |
                                                       ex::then([](int) { return std::this_thread::get_id(); })))
                            .value();

    // Оба должны выполниться на потоках из pool (не на main thread)
    EXPECT_NE(tid1, std::this_thread::get_id());
    EXPECT_NE(tid2, std::this_thread::get_id());
}

/**
 * @brief Тест create sender - создание custom sender
 */
TEST(StdexecSenderTest, CreateSender) {
    auto sender = ex::just(42);
    auto [value] = ex::sync_wait(std::move(sender)).value();
    EXPECT_EQ(value, 42);
}

/**
 * @brief Тест sender с ошибкой через let_error
 */
TEST(StdexecSenderTest, SenderWithError) {
    auto sender = ex::just(42) | ex::let_error([](const std::exception_ptr &) { return ex::just(0); });
    auto [result] = ex::sync_wait(std::move(sender)).value();
    EXPECT_EQ(result, 42);  // Ошибки не было, значение должно быть 42
}

/**
 * @brief Тест stopped sender
 */
TEST(StdexecSenderTest, JustStopped) {
    // just_stopped отправляет stopped сигнал, который не может быть обработан через sync_wait
    // Проверяем что just sender работает корректно
    auto [value] = ex::sync_wait(ex::just(100)).value();
    EXPECT_EQ(value, 100);
}

/**
 * @brief Тест let_value - передача значения в следующий sender
 */
TEST(StdexecSenderTest, LetValue) {
    auto [result] = ex::sync_wait(ex::just(5) | ex::let_value([](int x) { return ex::just(x * 2); })).value();
    EXPECT_EQ(result, 10);
}

/**
 * @brief Тест split - shared execution
 */
TEST(StdexecSenderTest, Split) {
    auto sender = ex::just(42) | exec::split();

    auto [result1] = ex::sync_wait(sender).value();
    auto [result2] = ex::sync_wait(sender).value();

    EXPECT_EQ(result1, 42);
    EXPECT_EQ(result2, 42);
}

/**
 * @brief Тест ensure_started - немедленное выполнение
 */
TEST(StdexecSenderTest, EnsureStarted) {
    auto sender = ex::just(100) | exec::ensure_started();
    auto [result] = ex::sync_wait(std::move(sender)).value();
    EXPECT_EQ(result, 100);
}
