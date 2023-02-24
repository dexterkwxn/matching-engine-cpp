#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

#include "instrument.hpp"

struct OrderBookNew {
  // TODO: use a concurrent hash map
  std::unordered_map<std::string, std::unique_ptr<InstrumentNew>> instruments;
  std::mutex instruments_mtx;

  std::unordered_map<uint32_t, InstrumentNew *> orders;
  std::mutex orders_mtx;

  std::atomic<intmax_t> timestamp;

  OrderBookNew() : timestamp{0} {}

  InstrumentNew &ensureInstrumentExists(std::string_view name) {
    // TODO: use an array of size 9 instead
    std::string name_str{name};
    auto &instrument = instruments[name_str];
    if (!instrument) {
      instrument = std::make_unique<InstrumentNew>(name_str, timestamp, orders,
                                                   orders_mtx);
    }
    return *instrument;
  }

  void processBuyOrder(uint32_t order_id, uint32_t price, uint32_t count,
                       std::string_view instrument_name) {
    std::lock_guard lock{orders_mtx};
    auto &instrument = ensureInstrumentExists(instrument_name);
    instrument.handleBuyOrder(order_id, price, count);
  }

  void processSellOrder(uint32_t order_id, uint32_t price, uint32_t count,
                        std::string_view instrument_name) {
    std::lock_guard lock{orders_mtx};
    auto &instrument = ensureInstrumentExists(instrument_name);
    instrument.handleSellOrder(order_id, price, count);
  }

  void processCancelOrder(uint32_t order_id) {
    std::lock_guard lock{orders_mtx};
    InstrumentNew *instrument;
    {
      auto it = orders.find(order_id);
      if (it == orders.end()) {
        Output::OrderDeleted(order_id, false, timestamp);
        ++timestamp;
        return;
      }
      instrument = it->second;
      orders.erase(it);
    }
    instrument->handleCancelOrder(order_id);
  }
};
