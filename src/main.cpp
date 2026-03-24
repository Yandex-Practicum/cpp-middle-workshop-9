#include <print>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "websocket_sender.hpp"
#include "exchange_api.hpp"

namespace ex = stdexec;
namespace exch = exchange;
namespace net = boost::asio;
namespace beast = boost::beast;

// Сендер 1 (подписка) -> Сендер 2 (чтение) -> Сендер 3 (печать подтверждения)
auto subscribe_and_print = [](auto& scheduler, auto& ws, std::string_view msg, std::string_view prefix) {
    return ex::on(scheduler,
        ws::subscribe_sender(ws, msg)
        // После успешной подписки → читаем ответ биржи
        | ex::let_value([&ws]() {
            return ws::async_read_sender(ws);
        })
        // Печатаем подтверждение
#warning "TODO: Воспользуйтесь ws::print_message_sender для печати полученного сообщения с префиксом."
        /*
            Ваш код здесь
        */
    );
};

int main() {
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

    std::println("🔌 Closing connections...");
    ex::sync_wait(ex::when_all(
        ex::on(scheduler, ws::close_sender(a_con)),
        ex::on(scheduler, ws::close_sender(b_con))
        ));
    pool.request_stop();
    std::println("🏁 All jobs completed");
    return 0;
}
