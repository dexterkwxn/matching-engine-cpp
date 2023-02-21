#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

#include "engine.hpp"
#include "io.hpp"

void _debug() { SyncCerr{} << '\n'; }
template <typename Head, typename... Tail> void _debug(Head H, Tail... T) {
  SyncCerr{} << ' ' << H;
  _debug(T...);
}
template <typename Head, typename... Tail> void debug(Head H, Tail... T) {
  SyncCerr{} << H;
  _debug(T...);
}

struct Order {
  uint32_t order_id;
  uint32_t price;
  uint32_t count;
  uint32_t execution_id;
  std::chrono::microseconds::rep timestamp; // is this the right type?
};

auto buy_order_cmp = [](const auto &x, const auto &y) {
  if (x.price != y.price) {
    return x.price > y.price;
  }
  if (x.timestamp != y.timestamp) {
    return x.timestamp < y.timestamp;
  }
  return x.order_id < y.order_id;
};
auto sell_order_cmp = [](const auto &x, const auto &y) {
  if (x.price != y.price) {
    return x.price < y.price;
  }
  if (x.timestamp != y.timestamp) {
    return x.timestamp < y.timestamp;
  }
  return x.order_id < y.order_id;
};

struct Instrument {
private:
  std::set<Order, decltype(buy_order_cmp)> buy_orders{buy_order_cmp};
  std::set<Order, decltype(sell_order_cmp)> sell_orders{sell_order_cmp};
  std::unordered_map<uint32_t, Order> orders;
  std::mutex mutex;

public:
  void handleCancelOrder(auto &&p_orders, uint32_t order_id, auto input_time) {
    std::lock_guard lock{mutex};

    auto it = orders.find(order_id);
    buy_orders.erase(it->second);
    sell_orders.erase(it->second);
    orders.erase(it);
    p_orders.erase(order_id);
    Output::OrderDeleted(order_id, true, input_time);
  }

  void handleBuyOrder(auto &&p_orders, std::string_view instrument,
                      uint32_t order_id, uint32_t price, uint32_t count,
                      std::chrono::microseconds::rep input_time) {
    std::lock_guard lock{mutex};

    Order buy_order{order_id, price, count, 1, input_time};

    while (buy_order.count && !sell_orders.empty()) {
      auto it = sell_orders.begin();
      auto sell_order = *it;
      if (sell_order.price > price) {
        break;
      }
      p_orders.erase(it->order_id);
      orders.erase(it->order_id);
      sell_orders.erase(it);

      auto exec_count = std::min(buy_order.count, sell_order.count);
      buy_order.count -= exec_count;
      sell_order.count -= exec_count;

      Output::OrderExecuted(sell_order.order_id, order_id,
                            sell_order.execution_id, sell_order.price,
                            exec_count, input_time);
      ++sell_order.execution_id;

      if (sell_order.count) {
        sell_orders.insert(sell_order);
        orders[sell_order.order_id] = sell_order;
        p_orders[sell_order.order_id] = this;
      }
    }

    if (buy_order.count) {
      buy_orders.insert(buy_order);
      orders[buy_order.order_id] = buy_order;
      p_orders[buy_order.order_id] = this;

      Output::OrderAdded(order_id, instrument.data(), price, buy_order.count,
                         false, input_time);
    }
  }

  void handleSellOrder(auto &&p_orders, std::string_view instrument,
                       uint32_t order_id, uint32_t price, uint32_t count,
                       std::chrono::microseconds::rep input_time) {
    std::lock_guard lock{mutex};

    Order sell_order{order_id, price, count, 1, input_time};

    while (sell_order.count && !buy_orders.empty()) {
      auto it = buy_orders.begin();
      auto buy_order = *it;
      if (buy_order.price < price) {
        break;
      }
      p_orders.erase(it->order_id);
      orders.erase(it->order_id);
      buy_orders.erase(it);

      auto exec_count = std::min(buy_order.count, sell_order.count);
      buy_order.count -= exec_count;
      sell_order.count -= exec_count;

      Output::OrderExecuted(buy_order.order_id, order_id,
                            buy_order.execution_id, buy_order.price, exec_count,
                            input_time);
      ++buy_order.execution_id;

      if (buy_order.count) {
        buy_orders.insert(buy_order);
        orders[buy_order.order_id] = buy_order;
        p_orders[buy_order.order_id] = this;
      }
    }

    if (sell_order.count) {
      sell_orders.insert(sell_order);
      orders[sell_order.order_id] = sell_order;
      p_orders[sell_order.order_id] = this;

      Output::OrderAdded(order_id, instrument.data(), price, sell_order.count,
                         true, input_time);
    }
  }
};

struct OrderBook {
  std::unordered_map<std::string, std::unique_ptr<Instrument>> instruments;
  std::unordered_map<uint32_t, Instrument *> orders;
  std::mutex mutex;

  Instrument &EnsureInstrumentExists(std::string_view name) {
    std::lock_guard lock{mutex};
    auto &instrument = instruments[std::string{name}];
    if (!instrument) {
      instrument = std::make_unique<Instrument>();
    }
    return *instrument;
  }

  void processCancelOrder(uint32_t order_id) {
    auto input_time = getCurrentTimestamp();
    auto it = orders.find(order_id);
    if (it == orders.end()) {
      Output::OrderDeleted(order_id, false, input_time);
      return;
    }
    it->second->handleCancelOrder(orders, order_id, input_time);
  }

  void processBuyOrder(uint32_t order_id, uint32_t price, uint32_t count,
                       std::string_view instrument_name) {
    auto input_time = getCurrentTimestamp();
    auto &instrument = EnsureInstrumentExists(instrument_name);
    instrument.handleBuyOrder(orders, instrument_name, order_id, price, count,
                              input_time);
  }

  void processSellOrder(uint32_t order_id, uint32_t price, uint32_t count,
                        std::string_view instrument_name) {
    auto input_time = getCurrentTimestamp();
    auto &instrument = EnsureInstrumentExists(instrument_name);
    instrument.handleSellOrder(orders, instrument_name, order_id, price, count,
                               input_time);
  }
};

OrderBook order_book;

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
      debug("Got cancel ID:", input.order_id);

      order_book.processCancelOrder(input.order_id);
      break;
    }

    case input_buy: {
      debug("Got buy order ID:", input.order_id);

      // Remember to take timestamp at the appropriate time, or compute
      // an appropriate timestamp!
      order_book.processBuyOrder(input.order_id, input.price, input.count,
                                 input.instrument);
      break;
    }

    case input_sell: {
      debug("Got sell order ID:", input.order_id);

      order_book.processSellOrder(input.order_id, input.price, input.count,
                                  input.instrument);
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
