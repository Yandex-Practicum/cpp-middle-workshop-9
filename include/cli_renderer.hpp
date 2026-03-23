#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/screen/screen.hpp>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>
#include <exec/repeat_until.hpp>

#include "types_core.hpp"
#include "arbitrage_spread_tracker.hpp"

// Размер графика
constexpr int kChartWidth = 100;
constexpr int kChartHeight = 80;
constexpr size_t max_history = kChartWidth;
constexpr auto kRefreshRate = std::chrono::milliseconds(100);

// Расширенные данные стакана
struct ExchangeOrderBook {
    std::string name;
    OrderBook orderbook;
    double best_bid;
    double best_ask;
};

namespace cli {

// Вспомогательные функции для конвертации
inline double price_to_double(const std::string& price) {
    return std::stod(price);
}

/**
 * @brief Renders a single frame of the terminal UI
 * @param spread_tracker Reference to the global spread tracker
 * @param refresh_rate_ms Screen refresh rate in milliseconds
 */
inline void render_frame(
    ArbitrageSpreadTracker<>& spread_tracker,
    std::chrono::milliseconds refresh_rate_ms = kRefreshRate)
{
    using namespace ftxui;
    // Consumer поток - читает из буферов и отрисовывает
    static int frame = 0;
    // Общие данные для графиков
    static std::vector<double> g_arb_spread_history;
    static std::vector<std::chrono::system_clock::time_point> g_time_history;

    // Локальные копии для отрисовки
    ExchangeOrderBook exchange1, exchange2;
    exchange1.name = "ServerA";
    exchange2.name = "ServerB";

    // Получаем актуальный спред из трекера
    auto spread = spread_tracker.get_spread();
    bool has_arb = spread_tracker.has_arbitrage_opportunity();

    // Читаем данные о биржах из трекера
    auto srv_a_state = spread_tracker.get_exchange_state(ExchangeId::ServerA);
    auto srv_b_state = spread_tracker.get_exchange_state(ExchangeId::ServerB);

    if (srv_a_state) {
        exchange1.best_bid = srv_a_state->best_bid.value_or(0.0);
        exchange1.best_ask = srv_a_state->best_ask.value_or(0.0);
        exchange1.orderbook.bids = srv_a_state->bids;
        exchange1.orderbook.asks = srv_a_state->asks;
    }
    if (srv_b_state) {
        exchange2.best_bid = srv_b_state->best_bid.value_or(0.0);
        exchange2.best_ask = srv_b_state->best_ask.value_or(0.0);
        exchange2.orderbook.bids = srv_b_state->bids;
        exchange2.orderbook.asks = srv_b_state->asks;
    }

    // Обновляем историю графиков при отрисовке
    double arb_spread = spread.best_bid && spread.best_ask
        ? ((spread.best_bid.value() - spread.best_ask.value()) / spread.best_ask.value()) * 100.0
        : 0.0;

    g_arb_spread_history.push_back(arb_spread);
    g_time_history.push_back(std::chrono::system_clock::now());

    if (g_arb_spread_history.size() > max_history) {
        g_arb_spread_history.erase(g_arb_spread_history.begin());
        g_time_history.erase(g_time_history.begin());
    }

    // Заголовок с арбитражной информацией
    std::string arb_status = has_arb ? " ⚡ ARB! " : " -- ";
    Color arb_status_color = has_arb ? Color::Green : Color::GrayDark;

    auto header = hbox({
        text(" 🚀 Crypto Spread Terminal - BTCUSDT ") | bgcolor(Color::DarkBlue) | bold,
        text(arb_status) | bgcolor(arb_status_color) | color(Color::White) | bold,
        filler(),
    });

    // === График арбитражного спреда ===
    std::string tab_name = "Arb Spread: Best Bid → Best Ask";

    double min_val = 0, max_val = 0.001, current_val = 0;

    if (g_arb_spread_history.empty()) {
        min_val = 0;
        max_val = 0.001;
        current_val = 0;
    } else {
        min_val = *std::min_element(g_arb_spread_history.begin(), g_arb_spread_history.end());
        max_val = *std::max_element(g_arb_spread_history.begin(), g_arb_spread_history.end());
        current_val = g_arb_spread_history.back();
    }

    double val_range = max_val - min_val;
    if (val_range < 0.0001) val_range = 0.0001;
    min_val -= val_range * 0.1;
    max_val += val_range * 0.1;
    val_range = max_val - min_val;

    std::ostringstream val_max_ss, val_min_ss, val_curr_ss;
    val_max_ss << std::fixed << std::setprecision(4) << max_val << "%";
    val_min_ss << std::fixed << std::setprecision(4) << min_val << "%";
    val_curr_ss << std::fixed << std::setprecision(4) << current_val << "%";

    Canvas chart(kChartWidth, kChartHeight);

    // Метки
    chart.DrawText(0, 0, val_max_ss.str(), Color::White);
    chart.DrawText(0, kChartHeight - 1, val_min_ss.str(), Color::White);

    // Нулевая линия (точка безубыточности)
    int zero_y = static_cast<int>((1.0 - (0.0 - min_val) / val_range) * (kChartHeight - 1));
    zero_y = std::clamp(zero_y, 0, kChartHeight - 1);
    for (int x = 0; x < kChartWidth; ++x) {
        chart.DrawPoint(x, zero_y, true, Color::GrayDark);
    }

    // Рисуем линию спреда
    for (size_t i = 1; i < g_arb_spread_history.size() && i < static_cast<size_t>(kChartWidth - 1); ++i) {
        int x1 = static_cast<int>(i - 1);
        int x2 = static_cast<int>(i);
        double val1 = g_arb_spread_history[i-1];
        double val2 = g_arb_spread_history[i];
        int y1 = static_cast<int>((1.0 - (val1 - min_val) / val_range) * (kChartHeight - 1));
        int y2 = static_cast<int>((1.0 - (val2 - min_val) / val_range) * (kChartHeight - 1));
        y1 = std::clamp(y1, 0, kChartHeight - 1);
        y2 = std::clamp(y2, 0, kChartHeight - 1);

        Color segment_color = (val1 >= 0) ? Color::Green : Color::Red;
        chart.DrawPointLine(x1, y1, x2, y2, segment_color);
    }

    Color current_color = current_val >= 0 ? Color::Green : Color::Red;

    auto tab_title = text(" " + tab_name + " ") | bgcolor(Color::Cyan) | color(Color::White) | bold;

    auto spread_info = hbox({
        text(" Current: ") | bold,
        text(val_curr_ss.str()) | color(current_color) | bold,
        text("  |  Min: ") | bold,
        text(val_min_ss.str()) | color(Color::Green),
        text("  |  Max: ") | bold,
        text(val_max_ss.str()) | color(Color::Red),
    });

    auto chart_box = vbox({
        tab_title,
        separator(),
        spread_info,
        separator(),
        canvas(&chart),
    }) | border | flex;

    // === Стакан биржи 1 (слева) ===
    constexpr size_t kOrderBookLevels = 5;
    Elements exchange1_elems;
    exchange1_elems.push_back(text(" " + exchange1.name + " ") | bgcolor(Color::Cyan) | color(Color::Black) | bold);
    std::ostringstream e1_best_ss;
    e1_best_ss << std::fixed << std::setprecision(2);
    e1_best_ss << "Best Bid: " << std::setw(10) << exchange1.best_bid << " | Best Ask: " << std::setw(10) << exchange1.best_ask;
    exchange1_elems.push_back(text(e1_best_ss.str()) | bold | color(Color::Yellow));
    exchange1_elems.push_back(text(" Asks (Sell) ") | color(Color::Red) | bold);
    // Asks: от высшего к низшему (reverse order)
    for (size_t i = 0; i < kOrderBookLevels; ++i) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        size_t idx = kOrderBookLevels - 1 - i;
        if (idx < exchange1.orderbook.asks.size()) {
            ss << std::setw(10) << price_to_double(exchange1.orderbook.asks[idx].price)
               << " | " << std::setprecision(4) << std::setw(12) << price_to_double(exchange1.orderbook.asks[idx].quantity);
        } else {
            ss << std::setw(10) << 0.0 << " | " << std::setprecision(4) << std::setw(12) << 0.0;
        }
        exchange1_elems.push_back(text(ss.str()) | color(Color::Red));
    }
    exchange1_elems.push_back(separator());
    for (size_t i = 0; i < kOrderBookLevels; ++i) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        if (i < exchange1.orderbook.bids.size()) {
            ss << std::setw(10) << price_to_double(exchange1.orderbook.bids[i].price)
               << " | " << std::setprecision(4) << std::setw(12) << price_to_double(exchange1.orderbook.bids[i].quantity);
        } else {
            ss << std::setw(10) << 0.0 << " | " << std::setprecision(4) << std::setw(12) << 0.0;
        }
        exchange1_elems.push_back(text(ss.str()) | color(Color::Green));
    }
    exchange1_elems.push_back(text(" Bids (Buy) ") | color(Color::Green) | bold);

    auto exchange1_box = vbox(exchange1_elems) | border;

    // === Стакан биржи 2 (справа) ===
    Elements exchange2_elems;
    exchange2_elems.push_back(text(" " + exchange2.name + " ") | bgcolor(Color::Magenta) | color(Color::White) | bold);
    std::ostringstream e2_best_ss;
    e2_best_ss << std::fixed << std::setprecision(2);
    e2_best_ss << "Best Bid: " << std::setw(10) << exchange2.best_bid << " | Best Ask: " << std::setw(10) << exchange2.best_ask;
    exchange2_elems.push_back(text(e2_best_ss.str()) | bold | color(Color::Yellow));
    exchange2_elems.push_back(text(" Asks (Sell) ") | color(Color::Red) | bold);
    // Asks: от высшего к низшему (reverse order)
    for (size_t i = 0; i < kOrderBookLevels; ++i) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        size_t idx = kOrderBookLevels - 1 - i;
        if (idx < exchange2.orderbook.asks.size()) {
            ss << std::setw(10) << price_to_double(exchange2.orderbook.asks[idx].price)
               << " | " << std::setprecision(4) << std::setw(12) << price_to_double(exchange2.orderbook.asks[idx].quantity);
        } else {
            ss << std::setw(10) << 0.0 << " | " << std::setprecision(4) << std::setw(12) << 0.0;
        }
        exchange2_elems.push_back(text(ss.str()) | color(Color::Red));
    }
    exchange2_elems.push_back(separator());
    for (size_t i = 0; i < kOrderBookLevels; ++i) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        if (i < exchange2.orderbook.bids.size()) {
            ss << std::setw(10) << price_to_double(exchange2.orderbook.bids[i].price)
               << " | " << std::setprecision(4) << std::setw(12) << price_to_double(exchange2.orderbook.bids[i].quantity);
        } else {
            ss << std::setw(10) << 0.0 << " | " << std::setprecision(4) << std::setw(12) << 0.0;
        }
        exchange2_elems.push_back(text(ss.str()) | color(Color::Green));
    }
    exchange2_elems.push_back(text(" Bids (Buy) ") | color(Color::Green) | bold);

    auto exchange2_box = vbox(exchange2_elems) | border;

    // Футер
    auto footer = text(" Frame: " + std::to_string(frame)) | dim | center;

    // Компоновка: Exchange1 | Chart | Exchange2
    auto doc = vbox({
        header,
        hbox({
            exchange1_box,
            chart_box | flex,
            exchange2_box,
        }),
        footer,
    });

    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(doc));
    Render(screen, doc);

    std::cout << "\033[H" << screen.ToString() << std::flush;
    frame++;
    std::this_thread::sleep_for(refresh_rate_ms);
}

}
