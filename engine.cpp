#include <algorithm>
#include <iostream>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

#include "engine.hpp"
#include "io.hpp"

void _debug() { std::cout << '\n'; }
template <typename Head, typename... Tail> void _debug(Head H, Tail... T) {
  std::cout << ' ' << H;
  _debug(T...);
}
template <typename Head, typename... Tail> void debug(Head H, Tail... T) {
  std::cout << H;
  _debug(T...);
}

struct Order {
  size_t order_id;
  size_t price;
  size_t count;
  size_t execution_id;
  std::chrono::microseconds::rep timestamp; // is this the right type?

  bool operator<(const Order &o) const {
    if (this->price != o.price) {
      return this->price < o.price;
    } else {
      return this->timestamp < o.timestamp;
    }
  }
};

struct Instrument {
private:
  std::string instrument_name;
  std::multiset<Order> buy_orders;
  std::multiset<Order> sell_orders;
  std::mutex instrument_mutex;

public:
  Instrument() {}
  Instrument(const Instrument &instrument_) { // copy constructor
    buy_orders = instrument_.buy_orders;
    sell_orders = instrument_.sell_orders;
    instrument_name = instrument_.instrument_name;
  }
  Instrument &operator=(const Instrument &instrument_) { // move constructor
    buy_orders = instrument_.buy_orders;
    sell_orders = instrument_.sell_orders;
    instrument_name = instrument_.instrument_name;
    return *this;
  }

  Instrument(std::string instrument_name_)
      : instrument_name(instrument_name_) {}

  void ProcessBuyOrder(std::string_view instrument, uint32_t order_id,
                       uint32_t price, uint32_t count,
                       std::chrono::microseconds::rep input_time) {
    std::lock_guard guard(instrument_mutex);

    Order buy_order = Order{order_id, price, count, 1, input_time};

    while (buy_order.count) {
      if (sell_orders.empty()) {
        buy_orders.insert(buy_order);
        auto output_time = getCurrentTimestamp();
        Output::OrderAdded(order_id, instrument.data(), price, buy_order.count,
                           false, output_time);
        break;
      }

      // matchy match im lazy to write this for now
      // okay now we have our sell_order
      auto it = sell_orders.begin();
      auto sell_order = *it;
      if (sell_order.price > price) {
        buy_orders.insert(buy_order);
        break;
      }
      sell_orders.erase(it);

      size_t count = std::min(buy_order.count, sell_order.count);
      buy_order.count -= count;
      sell_order.count -= count;

      auto output_time = getCurrentTimestamp();
      Output::OrderExecuted(sell_order.order_id, order_id,
                            sell_order.execution_id, sell_order.price, count,
                            output_time);

      if (sell_order.count) {
        sell_orders.insert(sell_order);
      }
    }
  }

  void ProcessSellOrder(std::string_view instrument, uint32_t order_id,
                        uint32_t price, uint32_t count,
                        std::chrono::microseconds::rep input_time) {
    std::lock_guard guard(instrument_mutex);

    Order sell_order = Order{order_id, price, count, 1, input_time};

    while (sell_order.count) {
      if (buy_orders.empty()) {
        sell_orders.insert(sell_order);
        auto output_time = getCurrentTimestamp();
        Output::OrderAdded(order_id, instrument.data(), price, sell_order.count,
                           true, output_time);
        break;
      }

      // matchy match im lazy to write this for now
      // okay now we have our sell_order
      auto lb =
          buy_orders.lower_bound(sell_order); // works depending on comparator<
      if (lb == buy_orders.end()) {
        sell_orders.insert(sell_order);
        break;
      }
      Order buy_order = *lb;
      sell_orders.erase(buy_order);

      size_t count = std::min(buy_order.count, sell_order.count);
      buy_order.count -= count;
      sell_order.count -= count;

      auto output_time = getCurrentTimestamp();
      Output::OrderExecuted(buy_order.order_id, order_id,
                            buy_order.execution_id, buy_order.price, count,
                            output_time);

      if (buy_order.count) {
        buy_orders.insert(buy_order);
      }
    }
  }
};

struct OrderBook {
  std::unordered_map<std::string, Instrument> instruments;
  std::mutex mutex;

  OrderBook() : instruments(), mutex() {}

  Instrument &EnsureInstrumentExists(std::string_view _name) {
    std::string name{_name};
    std::lock_guard guard{mutex};
    if (instruments.contains(name)) {
      return instruments[name];
    } else {
      return instruments[name] = Instrument{name};
    }
  }

  void ProcessBuyOrder(uint32_t order_id, uint32_t price, uint32_t count,
                       std::string_view instrument_name,
                       std::chrono::microseconds::rep input_time) {
    auto &instrument = EnsureInstrumentExists(instrument_name);
    instrument.ProcessBuyOrder(instrument_name, order_id, price, count,
                               input_time);
  }

  void ProcessSellOrder(uint32_t order_id, uint32_t price, uint32_t count,
                        std::string_view instrument_name,
                        std::chrono::microseconds::rep input_time) {
    auto &instrument = EnsureInstrumentExists(instrument_name);
    instrument.ProcessSellOrder(instrument_name, order_id, price, count,
                                input_time);
  }
};

OrderBook order_book = OrderBook();

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
      SyncCerr{} << "Got cancel: ID: " << input.order_id << std::endl;

      // Remember to take timestamp at the appropriate time, or compute
      // an appropriate timestamp!
      auto output_time = getCurrentTimestamp();
      Output::OrderDeleted(input.order_id, true, output_time);
      break;
    }

    case input_buy: {
      auto input_time = getCurrentTimestamp();
      order_book.ProcessBuyOrder(input.order_id, input.price, input.count,
                                 input.instrument, input_time);
      break;
    }

    case input_sell: {
      auto input_time = getCurrentTimestamp();
      order_book.ProcessSellOrder(input.order_id, input.price, input.count,
                                  input.instrument, input_time);
      break;
    }

    default: {
      SyncCerr{} << "Got order: " << static_cast<char>(input.type) << " "
                 << input.instrument << " x " << input.count << " @ "
                 << input.price << " ID: " << input.order_id << std::endl;

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
