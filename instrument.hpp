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

#include "io.hpp"

enum SIDE { BUY, SELL };

struct OrderNew {
  uint32_t id;
  uint32_t price;
  uint32_t count;
  uint32_t execution_id;
};

struct LimitNew {
  std::list<OrderNew> orders;
  std::mutex orders_mtx;
};

struct InstrumentNew {
  // TODO: use a concurrent BST
  std::map<uint32_t, std::unique_ptr<LimitNew>, std::greater<uint32_t>>
      buy_limits; // greatest price is at begin()
  std::mutex buy_limits_mtx;
  std::map<uint32_t, std::unique_ptr<LimitNew>> sell_limits;
  std::mutex sell_limits_mtx;

  std::unordered_map<uint32_t, std::list<OrderNew>::iterator> buy_orders;
  std::unordered_map<uint32_t, std::list<OrderNew>::iterator> sell_orders;

  std::mutex instrument_mtx;

  std::string name;

  std::atomic<intmax_t> &timestamp;

  std::unordered_map<uint32_t, InstrumentNew *> &global_orders;
  std::mutex &global_orders_mtx;

  InstrumentNew(std::string _name, std::atomic<intmax_t> &_timestamp,
                std::unordered_map<uint32_t, InstrumentNew *> &_global_orders,
                std::mutex &_global_orders_mtx)
      : name{_name}, timestamp{_timestamp}, global_orders{_global_orders},
        global_orders_mtx{_global_orders_mtx} {}

  LimitNew &ensureLimitExists(uint32_t price, bool is_sell) {
    auto &limit = is_sell ? sell_limits[price] : buy_limits[price];
    if (!limit) {
      limit = std::make_unique<LimitNew>();
    }
    return *limit;
  }

  void handleBuyOrSellOrder(uint32_t order_id, uint32_t price, uint32_t count,
                            auto &&opp_limits, bool is_sell) {
    OrderNew order{order_id, price, count, 1};
    while (order.count) {
      auto limit_it = opp_limits.begin();
      if (limit_it == opp_limits.end()) {
        // No more opp orders.
        break;
      }

      auto &[opp_price, opp_limit] = *limit_it;
      if ((is_sell && opp_price < price) || (!is_sell && opp_price > price)) {
        // Opp price does not match.
        break;
      }

      auto &opp_order = opp_limit->orders.front();
      auto matched_count = std::min(order.count, opp_order.count);
      order.count -= matched_count;
      opp_order.count -= matched_count;
      Output::OrderExecuted(opp_order.id, order_id, opp_order.execution_id,
                            opp_order.price, matched_count, timestamp);
      ++opp_order.execution_id;
      ++timestamp;

      if (!opp_order.count) {
        {
          global_orders.erase(opp_order.id);

          if (is_sell) {
            buy_orders.erase(opp_order.id);
          } else {
            sell_orders.erase(opp_order.id);
          }
        }
        opp_limit->orders.pop_front();
        if (opp_limit->orders.empty()) {
          opp_limits.erase(limit_it);
        }
      }
    }

    if (order.count) {
      auto &limit = ensureLimitExists(price, is_sell);
      limit.orders.push_back(order);

      if (is_sell) {
        sell_orders[order.id] = prev(limit.orders.end());
      } else {
        buy_orders[order.id] = prev(limit.orders.end());
      }

      { global_orders[order.id] = this; }

      Output::OrderAdded(order_id, name.c_str(), price, order.count, is_sell,
                         timestamp);
      ++timestamp;
    }
  }

  void handleBuyOrder(uint32_t order_id, uint32_t price, uint32_t count) {
    handleBuyOrSellOrder(order_id, price, count, sell_limits, BUY);
  }

  void handleSellOrder(uint32_t order_id, uint32_t price, uint32_t count) {
    handleBuyOrSellOrder(order_id, price, count, buy_limits, SELL);
  }

  void handleCancelOrder(uint32_t order_id) {
    auto it = buy_orders.find(order_id);
    if (it != buy_orders.end()) {
      auto order_it = it->second;
      auto limit_it = buy_limits.find(order_it->price);
      auto &orders = limit_it->second->orders;
      orders.erase(order_it);
      if (orders.empty()) {
        buy_limits.erase(limit_it);
      }
    } else {
      it = sell_orders.find(order_id);
      auto order_it = it->second;
      auto limit_it = sell_limits.find(order_it->price);
      auto &orders = limit_it->second->orders;
      orders.erase(order_it);
      if (orders.empty()) {
        sell_limits.erase(limit_it);
      }
    }
    Output::OrderDeleted(order_id, true, timestamp);
    ++timestamp;
  }
};
