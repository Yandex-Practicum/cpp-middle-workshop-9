/**
 * @file arbitrage_spread_tracker.hpp
 * @brief Трекер арбитражного спреда между двумя биржами
 *
 * Хранит стаканы заявок с двух бирж в OrderBookBuffer и автоматически
 * обновляет актуальное значение арбитражного спреда при добавлении новых данных.
 */

#pragma once

#include "order_book_buffer.hpp"
#include "types_core.hpp"
#include <optional>
#include <functional>
#include <string_view>
#include <mutex>

/**
 * @brief Конвертация ExchangeId в строку
 */
[[nodiscard]]
inline auto exchange_to_string(ExchangeId id) -> std::string_view {
    switch (id) {
        case ExchangeId::ServerA:   return "ServerA";
        case ExchangeId::ServerB:   return "ServerB";
        default:                    return "Unknown";
    }
}

/**
 * @brief Данные о текущем арбитражном спреде между биржами
 */
struct SpreadData {
    /// Лучшая цена покупки (max из всех bid)
    std::optional<double> best_bid;
    /// Лучшая цена продажи (min из всех ask)
    std::optional<double> best_ask;
    /// Абсолютный спред (best_ask - best_bid)
    std::optional<double> absolute_spread;
    /// Относительный спред в процентах ((best_ask - best_bid) / best_bid * 100)
    std::optional<double> relative_spread_percent;
    /// Биржа с лучшей ценой покупки
    ExchangeId best_bid_exchange;
    /// Биржа с лучшей ценой продажи
    ExchangeId best_ask_exchange;
    /// Временная метка последнего обновления
    Timestamp timestamp;

    /// Признак наличия арбитражной возможности (когда bid на одной бирже > ask на другой)
    [[nodiscard]]
    auto has_arbitrage_opportunity() const -> bool {
        return best_bid && best_ask && (best_bid.value() > best_ask.value());
    }

    /// Потенциальная прибыль от арбитража (в процентах)
    [[nodiscard]]
    auto arbitrage_profit_percent() const -> std::optional<double> {
        if (!has_arbitrage_opportunity()) {
            return std::nullopt;
        }
        return ((best_bid.value() - best_ask.value()) / best_ask.value()) * 100.0;
    }
};

/**
 * @brief Агрегированные данные стакана от одной биржи
 */
struct ExchangeOrderBookState {
    ExchangeId exchange_id;
    std::optional<double> best_bid;
    std::optional<double> best_ask;
    std::optional<double> mid_price;
    Timestamp last_update;
    bool has_data;
    std::vector<OrderBookLevel> bids;  // Лучшие уровни покупок
    std::vector<OrderBookLevel> asks;  // Лучшие уровни продаж

    ExchangeOrderBookState()
        : exchange_id(ExchangeId::Unknown)
        , best_bid(std::nullopt)
        , best_ask(std::nullopt)
        , mid_price(std::nullopt)
        , last_update(Timestamp{})
        , has_data(false) {}

    explicit ExchangeOrderBookState(ExchangeId id)
        : exchange_id(id)
        , best_bid(std::nullopt)
        , best_ask(std::nullopt)
        , mid_price(std::nullopt)
        , last_update(Timestamp{})
        , has_data(false) {}

    /**
     * @brief Обновить состояние из snapshot
     */
    void update(const OrderBook& snapshot) {
        // Считаем значение валидным только если оно > 0
        if (snapshot.best_bid > 0.0) {
            best_bid = snapshot.best_bid;
        } else {
            best_bid = std::nullopt;
        }

        if (snapshot.best_ask > 0.0) {
            best_ask = snapshot.best_ask;
        } else {
            best_ask = std::nullopt;
        }

        if (best_bid && best_ask) {
            mid_price = (best_bid.value() + best_ask.value()) / 2.0;
        } else {
            mid_price = std::nullopt;
        }

        last_update = snapshot.timestamp;
        has_data = best_bid.has_value() || best_ask.has_value();

        // Копируем уровни стакана для отображения
        bids = snapshot.bids;
        asks = snapshot.asks;
    }
};

/**
 * @brief Трекер арбитражного спреда между двумя биржами
 *
 * Использует два OrderBookBuffer для хранения данных от каждой биржи.
 * При добавлении новых данных в любой из буферов автоматически
 * пересчитывает арбитражный спред.
 * @tparam BufferCapacity Размер буферов для каждой биржи (по умолчанию 10)
 */
template<size_t BufferCapacity = 10>
class ArbitrageSpreadTracker {
public:
    using SpreadCallback = std::function<void(const SpreadData&)>;

    /**
     * @brief Конструктор
     * @param exchange1 Первая биржа
     * @param exchange2 Вторая биржа
     */
    explicit ArbitrageSpreadTracker(
        ExchangeId exchange1 = ExchangeId::ServerA,
        ExchangeId exchange2 = ExchangeId::ServerB)
        : exchange1_(exchange1)
        , exchange2_(exchange2)
        , state1_(exchange1)
        , state2_(exchange2) {}

    /**
     * @brief Добавить данные стакана от указанной биржи
     * @param exchange Идентификатор биржи
     * @param snapshot Данные стакана
     * @return true если данные успешно добавлены
     */
    auto add_exchange_data(ExchangeId exchange, const OrderBook& snapshot) -> bool {
        std::lock_guard<std::mutex> lock(mutex_);

        if (exchange == exchange1_) {
            buffer1_.push(snapshot);
            state1_.update(snapshot);
            recalculate_spread();
            return true;
        } else if (exchange == exchange2_) {
            buffer2_.push(snapshot);
            state2_.update(snapshot);
            recalculate_spread();
            return true;
        }
        return false;
    }

    /**
     * @brief Добавить данные стакана (move версия)
     * @param exchange Идентификатор биржи
     * @param snapshot Данные стакана
     * @return true если данные успешно добавлены
     */
    auto add_exchange_data(ExchangeId exchange, OrderBook&& snapshot) -> bool {
        std::lock_guard<std::mutex> lock(mutex_);

        if (exchange == exchange1_) {
            buffer1_.push(std::move(snapshot));
            state1_.update(buffer1_.peek().value());
            recalculate_spread();
            return true;
        } else if (exchange == exchange2_) {
            buffer2_.push(std::move(snapshot));
            state2_.update(buffer2_.peek().value());
            recalculate_spread();
            return true;
        }
        return false;
    }

    /**
     * @brief Получить текущие данные о спреде
     * @return SpreadData Текущий спред
     */
    [[nodiscard]]
    auto get_spread() const -> SpreadData {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_spread_;
    }

    /**
     * @brief Проверить наличие арбитражной возможности
     * @return true если bid на одной бирже > ask на другой
     */
    [[nodiscard]]
    auto has_arbitrage_opportunity() const -> bool {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_spread_.has_arbitrage_opportunity();
    }

    /**
     * @brief Получить состояние стакана от конкретной биржи
     * @param exchange Идентификатор биржи
     * @return ExchangeOrderBookState Состояние или пустое если нет данных
     */
    [[nodiscard]]
    auto get_exchange_state(ExchangeId exchange) const -> std::optional<ExchangeOrderBookState> {
        std::lock_guard<std::mutex> lock(mutex_);
        if (exchange == exchange1_ && state1_.has_data) {
            return state1_;
        } else if (exchange == exchange2_ && state2_.has_data) {
            return state2_;
        }
        return std::nullopt;
    }

    /**
     * @brief Сбросить все данные
     */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer1_.clear();
        buffer2_.clear();
        state1_ = ExchangeOrderBookState(exchange1_);
        state2_ = ExchangeOrderBookState(exchange2_);
        current_spread_ = SpreadData{};
    }

private:
    /**
     * @brief Пересчитать спред на основе текущих данных
     */
    void recalculate_spread() {
        SpreadData spread;
        spread.timestamp = std::chrono::system_clock::now();

        std::optional<double> best_bid_price;
        std::optional<double> best_ask_price;
        ExchangeId best_bid_exch = ExchangeId::Unknown;
        ExchangeId best_ask_exch = ExchangeId::Unknown;

        // Сравниваем best bid между биржами
        if (state1_.best_bid && state2_.best_bid) {
            if (state1_.best_bid.value() >= state2_.best_bid.value()) {
                best_bid_price = state1_.best_bid;
                best_bid_exch = exchange1_;
            } else {
                best_bid_price = state2_.best_bid;
                best_bid_exch = exchange2_;
            }
        } else if (state1_.best_bid) {
            best_bid_price = state1_.best_bid;
            best_bid_exch = exchange1_;
        } else if (state2_.best_bid) {
            best_bid_price = state2_.best_bid;
            best_bid_exch = exchange2_;
        }

        // Сравниваем best ask между биржами
        if (state1_.best_ask && state2_.best_ask) {
            if (state1_.best_ask.value() <= state2_.best_ask.value()) {
                best_ask_price = state1_.best_ask;
                best_ask_exch = exchange1_;
            } else {
                best_ask_price = state2_.best_ask;
                best_ask_exch = exchange2_;
            }
        } else if (state1_.best_ask) {
            best_ask_price = state1_.best_ask;
            best_ask_exch = exchange1_;
        } else if (state2_.best_ask) {
            best_ask_price = state2_.best_ask;
            best_ask_exch = exchange2_;
        }

        spread.best_bid = best_bid_price;
        spread.best_ask = best_ask_price;
        spread.best_bid_exchange = best_bid_exch;
        spread.best_ask_exchange = best_ask_exch;

        // Вычисляем спред
        if (best_bid_price && best_ask_price) {
            spread.absolute_spread = best_ask_price.value() - best_bid_price.value();

            if (best_bid_price.value() > 0) {
                spread.relative_spread_percent =
                    (spread.absolute_spread.value() / best_bid_price.value()) * 100.0;
            }
        }

        current_spread_ = spread;
    }

    ExchangeId exchange1_;
    ExchangeId exchange2_;

    /// Буферы для хранения истории snapshots от каждой биржи
    OrderBookBuffer<BufferCapacity> buffer1_;
    OrderBookBuffer<BufferCapacity> buffer2_;
    /// Текущее состояние каждой биржи (кэшированные лучшие цены)
    ExchangeOrderBookState state1_;
    ExchangeOrderBookState state2_;
    /// Текущий рассчитанный спред
    SpreadData current_spread_;
    mutable std::mutex mutex_;
};
