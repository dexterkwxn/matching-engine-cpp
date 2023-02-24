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

struct Order {
  uint32_t order_id;
  uint32_t price;
  uint32_t count;
  uint32_t execution_id;
  intmax_t timestamp;
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
  void handleCancelOrder(uint32_t order_id, std::atomic<intmax_t> &timestamp) {
    std::lock_guard lock{mutex};

    auto it = orders.find(order_id);
    if (it == orders.end()) {
      Output::OrderDeleted(order_id, false, timestamp);
      ++timestamp;
      return;
    }
    buy_orders.erase(it->second);
    sell_orders.erase(it->second);
    orders.erase(it);
    Output::OrderDeleted(order_id, true, timestamp);
    ++timestamp;
  }

  void handleBuyOrSellOrder(auto &&p_orders, std::mutex &p_orders_mtx,
                            std::string_view instrument, uint32_t order_id,
                            uint32_t price, uint32_t count,
                            std::atomic<intmax_t> &timestamp,
                            auto &&opp_side_orders, auto &&same_side_orders,
                            bool is_sell) {
    std::lock_guard lock{mutex};

    Order active_order{order_id, price, count, 1, 0};

    while (active_order.count && !opp_side_orders.empty()) {
      auto it = opp_side_orders.begin();
      auto matched_order = *it;
      if ((is_sell && matched_order.price < price) ||
          (!is_sell && matched_order.price > price)) {
        break;
      }
      orders.erase(it->order_id);
      opp_side_orders.erase(it);

      auto exec_count = std::min(active_order.count, matched_order.count);
      active_order.count -= exec_count;
      matched_order.count -= exec_count;

      Output::OrderExecuted(matched_order.order_id, order_id,
                            matched_order.execution_id, matched_order.price,
                            exec_count, timestamp);
      ++timestamp;
      ++matched_order.execution_id;

      if (matched_order.count) {
        opp_side_orders.insert(matched_order);
        orders[matched_order.order_id] = matched_order;
        {
          std::lock_guard p_lock{p_orders_mtx};
          p_orders[matched_order.order_id] = this;
        }
      }
    }

    if (active_order.count) {
      active_order.timestamp = timestamp;
      same_side_orders.insert(active_order);
      orders[active_order.order_id] = active_order;
      {
        std::lock_guard p_lock{p_orders_mtx};
        p_orders[active_order.order_id] = this;
      }

      Output::OrderAdded(order_id, instrument.data(), price, active_order.count,
                         is_sell, timestamp);
      ++timestamp;
    }
  }

  void handleBuyOrder(auto &&p_orders, std::mutex &p_orders_mtx,
                      std::string_view instrument, uint32_t order_id,
                      uint32_t price, uint32_t count,
                      std::atomic<intmax_t> &timestamp) {
    handleBuyOrSellOrder(p_orders, p_orders_mtx, instrument, order_id, price,
                         count, timestamp, sell_orders, buy_orders, false);
  }

  void handleSellOrder(auto &&p_orders, std::mutex &p_orders_mtx,
                       std::string_view instrument, uint32_t order_id,
                       uint32_t price, uint32_t count,
                       std::atomic<intmax_t> &timestamp) {
    handleBuyOrSellOrder(p_orders, p_orders_mtx, instrument, order_id, price,
                         count, timestamp, buy_orders, sell_orders, true);
  }
};

struct OrderBook {
  std::unordered_map<std::string, std::unique_ptr<Instrument>> instruments;
  std::unordered_map<uint32_t, Instrument *> orders;
  std::mutex instrument_mtx;
  std::mutex orders_mtx;
  std::atomic<intmax_t> timestamp{0};

  Instrument &EnsureInstrumentExists(std::string_view name) {
    std::lock_guard lock{instrument_mtx};
    auto &instrument = instruments[std::string{name}];
    if (!instrument) {
      instrument = std::make_unique<Instrument>();
    }
    return *instrument;
  }

  void processCancelOrder(uint32_t order_id) {
    decltype(orders)::iterator it;
    {
      std::lock_guard lock{orders_mtx};
      it = orders.find(order_id);
      if (it == orders.end()) {
        Output::OrderDeleted(order_id, false, timestamp);
        ++timestamp;
        return;
      }
    }
    it->second->handleCancelOrder(order_id, timestamp);
  }

  void processBuyOrder(uint32_t order_id, uint32_t price, uint32_t count,
                       std::string_view instrument_name) {
    auto &instrument = EnsureInstrumentExists(instrument_name);
    instrument.handleBuyOrder(orders, orders_mtx, instrument_name, order_id,
                              price, count, timestamp);
  }

  void processSellOrder(uint32_t order_id, uint32_t price, uint32_t count,
                        std::string_view instrument_name) {
    auto &instrument = EnsureInstrumentExists(instrument_name);
    instrument.handleSellOrder(orders, orders_mtx, instrument_name, order_id,
                               price, count, timestamp);
  }
};

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
