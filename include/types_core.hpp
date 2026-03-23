/**
 * @file types_core.hpp
 * @brief Базовые типы данных
 */

#pragma once

#include <string>
#include <chrono>
#include <vector>

/**
 * @brief Символ торговой пары
 */
using Symbol = std::string;

/**
 * @brief Цена в виде строки для точности (избегаем float precision issues)
 */
using Price = std::string;

/**
 * @brief Количество актива
 */
using Quantity = std::string;

/**
 * @brief Временная метка (timestamp from exchange)
 */
using Timestamp = std::chrono::system_clock::time_point;


/**
 * @brief Типы поддерживаемых бирж
 */
enum class ExchangeId {
    ServerA,
    ServerB,
    Unknown
};

/**
 * @brief Уровень в стакане (order book)
 */
struct OrderBookLevel {
    Price price;
    Quantity quantity;
};

/**
 * @brief Стакан заявок (order book snapshot)
 */
struct OrderBook {
    ExchangeId exchange_id;
    Symbol symbol;
    std::vector<OrderBookLevel> bids;  // Покупки (descending)
    std::vector<OrderBookLevel> asks;  // Продажи (ascending)
    Timestamp timestamp;
    uint64_t last_update_id;
    double best_bid;
    double best_ask;
};

/**
 * @brief Результат подключения к WebSocket
 */
struct ConnectionResult {
    bool success;
    std::string error_message;
};
