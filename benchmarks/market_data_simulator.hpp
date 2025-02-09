#pragma once
#include "../include/tick_capture/types.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <random>
#include <tbb/concurrent_hash_map.h>
#include <thread>

namespace tick_capture::benchmark {

class MarketDataSimulator {
public:
  struct Config {
    // Network settings
    std::string multicast_addr{"239.255.0.1"};
    uint16_t port{12345};

    // Simulation settings
    uint32_t num_symbols{100};     // Number of symbols to simulate
    uint32_t base_msg_rate{1000};  // Base messages per second
    uint32_t burst_size{0};        // Size of bursts (0 to disable)
    uint32_t burst_interval{1000}; // Milliseconds between bursts

    // Market settings
    double price_volatility{0.001}; // Price change std dev
    uint32_t min_trade_size{100};
    uint32_t max_trade_size{10000};
    double min_price{10.0};          // Minimum allowed price
    double max_price{1000.0};        // Maximum allowed price
    double initial_price_min{100.0}; // Starting price range
    double initial_price_max{500.0};
  };

  explicit MarketDataSimulator(const Config &config);
  ~MarketDataSimulator();

  // Non-copyable
  MarketDataSimulator(const MarketDataSimulator &) = delete;
  MarketDataSimulator &operator=(const MarketDataSimulator &) = delete;

  void start();
  void stop();

  // Get statistics about sent messages
  struct Stats {
    uint64_t messages_sent{0};
    uint64_t current_rate{0};
    uint64_t messages_dropped{0};
  };
  Stats get_stats() const;

  // Message tracking for verification
  using MessageLog = tbb::concurrent_hash_map<uint64_t, MarketMessage>;
  const MessageLog &get_message_log() const { return message_log_; }

private:
  void run_simulation();
  MarketMessage generate_message();
  bool send_message(const MarketMessage &msg);
  void init_symbol_states();

  Config config_;
  boost::asio::io_context io_context_;
  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint multicast_endpoint_;

  // Threading
  std::atomic<bool> running_{false};
  std::thread sim_thread_;

  // Message tracking
  MessageLog message_log_;
  std::atomic<uint64_t> sequence_number_{0};
  std::atomic<uint64_t> messages_sent_{0};
  std::atomic<uint64_t> messages_dropped_{0};
  std::atomic<uint64_t> current_rate_{0};

  // Market state
  struct SymbolState {
    double last_price;
    uint32_t last_size;
    std::chrono::nanoseconds last_update;
  };
  std::vector<SymbolState> symbol_states_;

  // RNG with good quality for market simulation
  std::mt19937_64 rng_{std::random_device{}()};
};

} // namespace tick_capture::benchmark
