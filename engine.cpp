#include <iostream>
#include <thread>
#include <queue>
#include <unordered_map>
#include <mutex>

#include "io.hpp"
#include "engine.hpp"

struct Order {
  std::string order_type;
  size_t qty;
  size_t price;
};

struct Instrument {
private:
  std::string instrument_name;
  std::priority_queue<Order> buy_queue;
  std::priority_queue<Order> sell_queue;
  std::mutex instrument_mutex;

public:
  Instrument() {}
  Instrument(const Instrument& instrument_) { // copy constructor
    buy_queue = instrument_.buy_queue;
    sell_queue = instrument_.sell_queue;
    instrument_name = instrument_.instrument_name;
  }
  Instrument& operator=(const Instrument& instrument_) { // move constructor
    buy_queue = instrument_.buy_queue;
    sell_queue = instrument_.sell_queue;
    instrument_name = instrument_.instrument_name;
    return *this;
  }

  Instrument(std::string instrument_name_): instrument_name(instrument_name_) {}

  void ProcessBuyOrder(uint32_t order_id, uint32_t price, uint32_t count) {
    std::lock_guard guard(instrument_mutex);

  }

};

struct OrderBook {
  std::unordered_map<std::string, Instrument> instruments;
  std::mutex mutex;

  OrderBook(): instruments(), mutex() {}

  void InstrumentExists(std::string instrument) {
    std::lock_guard guard(mutex);
    if (instruments.contains(instrument)) {
      return;
    } else {
      instruments[instrument] = Instrument(instrument);
    }
  }

  void ProcessBuyOrder(uint32_t order_id, uint32_t price, uint32_t count, std::string instrument) {
    InstrumentExists(instrument);
    instruments[instrument].ProcessBuyOrder(order_id, price, count);
  }
};

OrderBook order_book = OrderBook();

void Engine::accept(ClientConnection connection)
{
	auto thread = std::thread(&Engine::connection_thread, this, std::move(connection));
	thread.detach();
}

void Engine::connection_thread(ClientConnection connection)
{
	while(true)
	{
		ClientCommand input {};
		switch(connection.readInput(input))
		{
			case ReadResult::Error: SyncCerr {} << "Error reading input" << std::endl;
			case ReadResult::EndOfFile: return;
			case ReadResult::Success: break;
		}

		// Functions for printing output actions in the prescribed format are
		// provided in the Output class:
		switch(input.type)
		{
			case input_cancel: {
				SyncCerr {} << "Got cancel: ID: " << input.order_id << std::endl;

				// Remember to take timestamp at the appropriate time, or compute
				// an appropriate timestamp!
				auto output_time = getCurrentTimestamp();
				Output::OrderDeleted(input.order_id, true, output_time);
				break;
			}

      case input_buy:

      case input_sell:

			default: {
				SyncCerr {}
				    << "Got order: " << static_cast<char>(input.type) << " " << input.instrument << " x " << input.count << " @ "
				    << input.price << " ID: " << input.order_id << std::endl;

				// Remember to take timestamp at the appropriate time, or compute
				// an appropriate timestamp!
				auto output_time = getCurrentTimestamp();
				Output::OrderAdded(input.order_id, input.instrument, input.price, input.count, input.type == input_sell,
				    output_time);
				break;
			}
		}

		// Additionally:

		// Remember to take timestamp at the appropriate time, or compute
		// an appropriate timestamp!
		intmax_t output_time = getCurrentTimestamp();

		// Check the parameter names in `io.hpp`.
		Output::OrderExecuted(123, 124, 1, 2000, 10, output_time);
	}
}
