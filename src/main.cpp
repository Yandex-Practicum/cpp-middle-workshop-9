#include <print>

#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "websocket_sender.hpp"
#include "exchange_api.hpp"

namespace ex = stdexec;
namespace exch = exchange;
namespace net = boost::asio;


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
    std::println("🔌 Closing connections...");
    ex::sync_wait(ex::when_all(
        ex::on(scheduler, ws::close_sender(a_con)),
        ex::on(scheduler, ws::close_sender(b_con))
        ));
    pool.request_stop();
    std::println("🏁 All jobs completed");
    return 0;
}
