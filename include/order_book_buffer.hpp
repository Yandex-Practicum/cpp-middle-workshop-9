#pragma once

#include <boost/circular_buffer.hpp>
#include <optional>
#include "types_core.hpp"

/**
 * @brief Локальный orderbook
 */
template<size_t Capacity = 10>
class OrderBookBuffer {
public:
    OrderBookBuffer() : buffer_(Capacity) {}

    /**
     * @brief Записать snapshot в буфер (producer)
     * @param snapshot Данные стакана
     */
    void push(const OrderBook& snapshot) {
        buffer_.push_back(snapshot);
    }

    /**
     * @brief Записать snapshot (move версия) (producer)
     * @param snapshot Данные стакана
     */
    void push(OrderBook&& snapshot) {
        buffer_.push_back(std::move(snapshot));
    }

    /**
     * @brief Прочитать последний snapshot (consumer)
     * @return std::optional<OrderBook> Данные если есть, std::nullopt если буфер пуст
     */
    std::optional<OrderBook> pop() {
        if (buffer_.empty()) {
            return std::nullopt;
        }
        OrderBook snapshot = std::move(buffer_.back());
        buffer_.pop_back();
        return snapshot;
    }

    /**
     * @brief Получить последний snapshot без удаления (consumer)
     * @return std::optional<OrderBook> Данные если есть, std::nullopt если буфер пуст
     */
    std::optional<OrderBook> peek() const {
        if (buffer_.empty()) {
            return std::nullopt;
        }
        return buffer_.back();
    }

    /**
     * @brief Получить текущее состояние локального orderbook (для Bybit)
     * @return std::optional<OrderBook> Текущее состояние или std::nullopt
     */
    std::optional<OrderBook> getLocalOrderBook() const {
        if (local_bids_.empty() && local_asks_.empty()) {
            return std::nullopt;
        }

        OrderBook snapshot;
        snapshot.exchange_id = local_exchange_id_;
        snapshot.symbol = local_symbol_;
        snapshot.bids = local_bids_;
        snapshot.asks = local_asks_;
        snapshot.timestamp = local_timestamp_;

        // Вычисляем best bid/ask
        snapshot.best_bid = local_bids_.empty() ? 0.0 : parsePrice(local_bids_.front().price);
        snapshot.best_ask = local_asks_.empty() ? 0.0 : parsePrice(local_asks_.front().price);

        return snapshot;
    }

    /**
     * @brief Проверить пустоту буфера
     */
    bool empty() const {
        return buffer_.empty();
    }

    /**
     * @brief Получить количество элементов
     */
    size_t size() const {
        return buffer_.size();
    }

    /**
     * @brief Очистить буфер
     */
    void clear() {
        buffer_.clear();
        clearLocalOrderBook();
    }


private:

    /**
     * @brief Очистить локальный orderbook
     */
    void clearLocalOrderBook() {
        local_bids_.clear();
        local_asks_.clear();
        local_symbol_.clear();
    }

    /**
     * @brief Парсить цену из строки
     */
    static double parsePrice(const Price& price) {
        try {
            return std::stod(price);
        } catch (...) {
            return 0.0;
        }
    }

    boost::circular_buffer<OrderBook, std::allocator<OrderBook>> buffer_;
    ExchangeId local_exchange_id_ = ExchangeId::Unknown;
    Symbol local_symbol_;
    std::vector<OrderBookLevel> local_bids_;
    std::vector<OrderBookLevel> local_asks_;
    Timestamp local_timestamp_;
};
