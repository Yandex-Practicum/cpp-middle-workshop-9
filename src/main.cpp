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

#warning "TODO: Параллельно подключаемся к обеим биржам"
    std::println("🔌 Closing connections...");
#warning "TODO: Graceful shutdown: параллельно закрываем соединения через close_sender"

    pool.request_stop();
    std::println("🏁 All jobs completed");
    return 0;
}
