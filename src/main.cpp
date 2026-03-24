#include <print>

#include <exec/repeat_until.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "types_core.hpp"
#include "cli_renderer.hpp"
#include "arbitrage_spread_tracker.hpp"
#include "websocket_sender.hpp"
#include "exchange_api.hpp"
#include "order_book_parser.hpp"

namespace ex = stdexec;
namespace exch = exchange;
namespace net = boost::asio;
namespace beast = boost::beast;

std::atomic<bool> g_running{true};
ex::inplace_stop_source g_stop_source;

void signal_handler(int) {
    std::println("🛑 Signal received, stopping...");
    g_running = false;
    g_stop_source.request_stop();
}

// Глобальный трекер арбитражного спреда (хранит стаканы обеих бирж)
ArbitrageSpreadTracker<> g_spread_tracker(
    ExchangeId::ServerA,
    ExchangeId::ServerB
);
// Сендер 1 (подписка) -> Сендер 2 (чтение) -> Сендер 3 (печать подтверждения)
auto subscribe_and_print = [](auto& scheduler, auto& ws, std::string_view msg, std::string_view prefix) {
    return ex::on(scheduler,
        ws::subscribe_sender(ws, msg)
        // После успешной подписки → читаем ответ биржи
        | ex::let_value([&ws]() {
            return ws::async_read_sender(ws);
        })
        // Печатаем подтверждение
        | ex::let_value([prefix](beast::flat_buffer buffer) {
            return ws::print_message_sender(std::move(buffer), prefix);
        })
    );
};

auto read_and_print = [](auto& scheduler, auto& ws, parser::orderbook_parser_factory auto&& parser) {
    return ex::on(scheduler,
        exec::repeat_until(
            ws::async_read_sender(ws)
            | ex::let_value([&parser](beast::flat_buffer buffer) {
                return std::forward<decltype(parser)>(parser)(std::move(buffer));
            })
#warning "TODO: добавить запись данных в трекер внутри цепочки sender-ов, используя ex::let_value и g_spread_tracker.add_exchange_data()"
#warning "TODO: добавить проверку g_stop_source.stop_requested() внутри ex::let_value и завершать цикл, при помощи return ex::just(true), при получении сигнала остановки. Для продолжения цикла использовать return ex::just(false)"
#warning "TODO: добавить обработку ошибок чтения и парсинга при помощи ex::let_error"
            /*
                Ваш код здесь:
                - внутри ex::let_value, после получения OrderBook, нужно вызвать g_spread_tracker.add_exchange_data(book) для обновления данных в трекере.
                - также нужно проверить g_stop_source.stop_requested() и если он запрошен, то завершить цикл, вернув ex::just(true).
                - если стоп не запрошен, то продолжить цикл, вернув ex::just(false).
                - не забудьте обработать возможные ошибки чтения и парсинга, используя ex::let_error для логирования ошибок и продолжения работы.
            */
        )
    );
};

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    net::io_context ioc{1};
    exec::static_thread_pool pool{8};
    const auto &scheduler = pool.get_scheduler();
    std::println("Connecting to exchanges...");
    auto [a_con, b_con] = ex::sync_wait(
        ex::when_all(
            ex::on(scheduler, ws::connect_sender(exch::kServerAConfig, std::ref(ioc))),
            ex::on(scheduler, ws::connect_sender(exch::kServerBConfig, std::ref(ioc)))
        )
    ).value();

    std::println("🔗 Both connections established, starting data processing...");
    // Оформляем подписку на обе биржи и читаем первое сообщение (подтверждение)
    ex::sync_wait(
        ex::when_all(
            subscribe_and_print(scheduler, *a_con,
            exch::make_orderbook_subscribe_message(exch::Symbol::ORNG), "📬 Server A confirmation: "),
            subscribe_and_print(scheduler, *b_con,
            exch::make_orderbook_subscribe_message(exch::Symbol::ORNG), "📬 Server B confirmation: ")
        )
    );

    std::println("✅ Subscriptions confirmed, starting data processing...");

    std::thread([&](){
            ex::sync_wait(
                ex::when_all(
                    read_and_print(scheduler, *a_con, parser::parse_serv_a_orderbook_sender),
                    read_and_print(scheduler, *b_con, parser::parse_serv_b_orderbook_sender)
                )
            );
        }
    ).detach();

    std::thread([&](){
        while (g_running)
        {
            cli::render_frame(g_spread_tracker);
        }
    }).join();
    std::println("🏁 Terminal shutdown complete");

    std::println("🔌 Closing connections...");
    ex::sync_wait(ex::when_all(
        ex::on(scheduler, ws::close_sender(a_con)),
        ex::on(scheduler, ws::close_sender(b_con))
        ));
    pool.request_stop();
    std::println("🏁 All jobs completed");
    return 0;
}
