#pragma once

#include <string>
#include "websocket_sender.hpp"

namespace exchange {

struct Symbol {
    static constexpr std::string_view ORNG = "Orange";
};

inline constexpr ws::ExchangeConfig kServerAConfig = {
    .host = "simulator",
    .port = "8080",
    .path = "/ws",
    .use_ssl = false,
    .exchange_name = "ServerA"
};

inline constexpr ws::ExchangeConfig kServerBConfig = {
    .host = "simulator",
    .port = "8081",
    .path = "/ws",
    .use_ssl = false,
    .exchange_name = "ServerB"
};

/**
 * @brief Сообщение подписки на orderbook (стакан)
 * @param symbol Торговая пара
 * @return JSON сообщение для подписки
 */
inline auto make_orderbook_subscribe_message(
    std::string_view symbol
) -> std::string {
    return std::format(R"({{"op": "subscribe", "args": ["orderbook.{}"]}})", symbol);
}

} // namespace exchange
