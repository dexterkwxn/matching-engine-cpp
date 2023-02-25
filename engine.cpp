#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

#include "engine.hpp"
#include "io.hpp"
#include "order_book.hpp"

void _debug() { SyncCerr{} << '\n'; }
template <typename Head, typename... Tail> void _debug(Head H, Tail... T) {
  SyncCerr{} << ' ' << H;
  _debug(T...);
}
template <typename Head, typename... Tail> void debug(Head H, Tail... T) {
  SyncCerr{} << H;
  _debug(T...);
}

OrderBookNew order_book;

void Engine::accept(ClientConnection connection) {
  auto thread =
      std::thread(&Engine::connection_thread, this, std::move(connection));
  thread.detach();
}

void Engine::connection_thread(ClientConnection connection) {
  while (true) {
    ClientCommand input{};
    switch (connection.readInput(input)) {
    case ReadResult::Error:
      SyncCerr{} << "Error reading input" << std::endl;
    case ReadResult::EndOfFile:
      return;
    case ReadResult::Success:
      break;
    }

    // Functions for printing output actions in the prescribed format are
    // provided in the Output class:
    switch (input.type) {
    case input_cancel: {
      order_book.processCancelOrder(input.order_id);
      break;
    }

    case input_buy: {
      // Remember to take timestamp at the appropriate time, or compute
      // an appropriate timestamp!
      order_book.processBuyOrder(input.order_id, input.price, input.count,
                                 input.instrument);
      break;
    }

    case input_sell: {
      order_book.processSellOrder(input.order_id, input.price, input.count,
                                  input.instrument);
      break;
    }

    default: {
      // Remember to take timestamp at the appropriate time, or compute
      // an appropriate timestamp!
      auto output_time = getCurrentTimestamp();
      Output::OrderAdded(input.order_id, input.instrument, input.price,
                         input.count, input.type == input_sell, output_time);
      break;
    }
    }

    // Additionally:

    // Remember to take timestamp at the appropriate time, or compute
    // an appropriate timestamp!
    /*
    intmax_t output_time = getCurrentTimestamp();

    // Check the parameter names in `io.hpp`.
    Output::OrderExecuted(123, 124, 1, 2000, 10, output_time);
    */
  }
}
