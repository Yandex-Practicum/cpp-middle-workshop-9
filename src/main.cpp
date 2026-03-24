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
/*
    Ваш код здесь: используйте connect_sender для подключения к обеим биржам и получения WebSocket stream'ов.
     - Не забудьте обернуть ioc в std::reference_wrapper при передаче в sender
     - Сохраните полученные WebSocket stream'ы для дальнейшего использования
*/
    std::println("🔌 Closing connections...");
#warning "TODO: Graceful shutdown: параллельно закрываем соединения через close_sender"
/*
    Ваш код здесь: используйте close_sender для закрытия обоих WebSocket соединений.
*/
    pool.request_stop();
    std::println("🏁 All jobs completed");
    return 0;
}
