#pragma once
#include "types_core.hpp"

#include <format>
#include <stdexec/execution.hpp>
#include <exec/create.hpp>

#include <nlohmann/json.hpp>

#include <boost/beast/core.hpp>

namespace parser {

namespace ex = stdexec;
namespace beast = boost::beast;

// ============================================================================
// Концепт: sender, который парсит OrderBook
// ============================================================================
template<typename S>
concept orderbook_parser_sender =
    ex::sender<S> &&  // базовая проверка: это sender
    ex::sender_of<S, ex::set_value_t(OrderBook)> &&  // может вернуть OrderBook
    ex::sender_of<S, ex::set_error_t(std::exception_ptr)> &&  // может вернуть ошибку
    ex::sender_of<S, ex::set_stopped_t()>;  // может быть остановлен


template<typename F>
concept orderbook_parser_factory =
    requires(F&& f, beast::flat_buffer buf) {
        { std::forward<F>(f)(std::move(buf)) } -> ex::sender;
        requires parser::orderbook_parser_sender<
            decltype(std::forward<F>(f)(std::move(buf)))
        >;
    };

/**
 * @brief Распарсить сообщение order book
 * @param json_str JSON строка со стаканом заявок
 * @return OrderBook структура
 */
[[nodiscard]]
inline OrderBook parse_serv_orderbook(std::string_view json_str, ExchangeId id){
    auto json = nlohmann::json::parse(json_str);

    OrderBook book;
    book.exchange_id = id;
    const auto& srv_id = json["serv_id"].get<int>();

    if ((book.exchange_id == ExchangeId::ServerA && srv_id != 8080) ||
        (book.exchange_id == ExchangeId::ServerB && srv_id != 8081)) {
        throw std::runtime_error(std::format(
            "Invalid server ID in JSON: expected {}, got {}",
            std::to_string((book.exchange_id == ExchangeId::ServerA) ? 8080 : 8081),
            srv_id));
    }

    book.symbol = json["symbol"];
    book.last_update_id = json.value("lastUpdateId", uint64_t{0});

    // Парсим bids (покупки)
    if (json.contains("bids")) {
        for (const auto& bid : json["bids"]) {
            if (bid.size() >= 2) {
                book.bids.push_back({
                    .price = bid[0].get<std::string>(),
                    .quantity = bid[1].get<std::string>()
                });
            }
        }
    }

    // Парсим asks (продажи)
    if (json.contains("asks")) {
        for (const auto& ask : json["asks"]) {
            if (ask.size() >= 2) {
                book.asks.push_back({
                    .price = ask[0].get<std::string>(),
                    .quantity = ask[1].get<std::string>()
                });
            }
        }
    }

    book.timestamp = std::chrono::system_clock::now();
    book.best_ask = book.asks.empty() ? 0.0 : std::stod(book.asks[0].price);
    book.best_bid = book.bids.empty() ? 0.0 : std::stod(book.bids[0].price);
    return book;
}

/**
 * @brief Sender для парсинга Server A order book из buffer
 * @note Для обработки сообщений от Server A (порт 8080) - используем parse_serv_orderbook с id = ServerA
 */
[[nodiscard]]
inline auto parse_serv_a_orderbook_sender(beast::flat_buffer buffer) -> orderbook_parser_sender auto {
    return exec::create<ex::completion_signatures<
        ex::set_value_t(OrderBook),
        ex::set_error_t(std::exception_ptr),
        ex::set_stopped_t()>>([buf = std::move(buffer)](auto ctx) noexcept {

        try {
            auto book = parse_serv_orderbook(beast::buffers_to_string(buf.data()), ExchangeId::ServerA);
            ex::set_value(std::move(ctx.receiver), std::move(book));
        } catch (...) {
            ex::set_error(std::move(ctx.receiver), std::current_exception());
        }
    });
}

/**
 * @brief Sender для парсинга Server B order book из buffer
 * @note Для обработки сообщений от Server B (порт 8081) - используем parse_serv_orderbook с id = ServerB
 */
[[nodiscard]]
inline auto parse_serv_b_orderbook_sender(beast::flat_buffer buffer) -> orderbook_parser_sender auto{
    return exec::create<ex::completion_signatures<
        ex::set_value_t(OrderBook),
        ex::set_error_t(std::exception_ptr),
        ex::set_stopped_t()>>([buf = std::move(buffer)](auto ctx) noexcept {

        try {
            auto book = parse_serv_orderbook(beast::buffers_to_string(buf.data()), ExchangeId::ServerB);
            ex::set_value(std::move(ctx.receiver), std::move(book));
        } catch (...) {
            ex::set_error(std::move(ctx.receiver), std::current_exception());
        }
    });
}

} // namespace parser
