#include "market_data_simulator.hpp"
#include <fmt/format.h>

namespace tick_capture::benchmark {

MarketDataSimulator::MarketDataSimulator(const Config &config)
    : config_(config), socket_(io_context_, boost::asio::ip::udp::v4()) {

  using namespace boost::asio::ip;

  // Configure socket
  socket_.set_option(udp::socket::reuse_address(true));
  socket_.set_option(boost::asio::ip::multicast::enable_loopback(true));
  socket_.set_option(
      boost::asio::socket_base::send_buffer_size(10 * 1024 * 1024));

  // Create endpoint
  boost::system::error_code ec;
  auto addr = boost::asio::ip::make_address(config.multicast_addr, ec);
  if (ec) {
    throw std::runtime_error(
        fmt::format("Invalid multicast address: {}", ec.message()));
  }
  multicast_endpoint_ = udp::endpoint(addr, config.port);

  // Initialize symbol states
  init_symbol_states();
}

MarketDataSimulator::~MarketDataSimulator() { stop(); }

void MarketDataSimulator::init_symbol_states() {
  symbol_states_.clear();
  symbol_states_.reserve(config_.num_symbols);

  std::uniform_real_distribution<double> price_dist(
      100.0, 500.0); // More realistic price range

  for (uint32_t i = 0; i < config_.num_symbols; ++i) {
    SymbolState state;
    state.last_price = price_dist(rng_);
    state.last_size = 1000; // Starting size
    state.last_update = std::chrono::nanoseconds(0);
    symbol_states_.push_back(state);
  }
}

void MarketDataSimulator::start() {
  if (running_)
    return;
  running_ = true;
  sim_thread_ = std::thread([this] { run_simulation(); });
}

void MarketDataSimulator::stop() {
  if (!running_)
    return;
  running_ = false;
  if (sim_thread_.joinable()) {
    sim_thread_.join();
  }
}

void MarketDataSimulator::run_simulation() {
  using namespace std::chrono;

  fmt::print("Starting simulator with rate: {} msgs/sec\n",
             config_.base_msg_rate);

  const auto base_interval = nanoseconds(1'000'000'000 / config_.base_msg_rate);
  auto next_send = steady_clock::now();

  uint64_t messages_this_second = 0;
  auto rate_reset = steady_clock::now() + seconds(1);
  size_t debug_count = 0; // For initial message debugging

  while (running_) {
    auto now = steady_clock::now();

    // Reset rate counter every second
    if (now >= rate_reset) {
      fmt::print("Simulator current rate: {} msgs/sec\n", messages_this_second);
      messages_this_second = 0;
      rate_reset += seconds(1);
    }

    // Send message if it's time
    if (now >= next_send) {
      auto msg = generate_message();

      // Debug first few messages
      if (debug_count < 5) {
        fmt::print(
            "Generated message {}: seq={}, sym={}, price={:.2f}, size={}\n",
            debug_count++, msg.sequence_number, msg.symbol_id, msg.trade.price,
            msg.trade.size);
      }

      if (send_message(msg)) {
        messages_this_second++;
        next_send += base_interval;
      } else {
        const auto dropped = ++messages_dropped_;
        // Brief backoff on error
        next_send += microseconds(100);
        fmt::print(stderr, "Failed to send message {}. Total dropped: {}\n",
                   msg.sequence_number, dropped);
      }
    }

    // Sleep if we're ahead of schedule
    if (auto sleep_time = next_send - steady_clock::now();
        sleep_time > nanoseconds(0)) {
      std::this_thread::sleep_for(sleep_time);
    }
  }

  const auto total_sent = messages_sent_.load();
  fmt::print("Simulator stopping. Total messages sent: {}\n", total_sent);
}

MarketMessage MarketDataSimulator::generate_message() {
  MarketMessage msg{}; // Zero-initialize

  // Set header fields
  msg.sequence_number = ++sequence_number_;
  msg.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();

  // Set message type
  msg.type = MessageType::Trade;

  // Select symbol (1-based)
  std::uniform_int_distribution<uint32_t> symbol_dist(1, config_.num_symbols);
  msg.symbol_id = symbol_dist(rng_);

  // Get symbol state
  auto &state = symbol_states_[msg.symbol_id - 1];

  // Update price with controlled volatility
  std::normal_distribution<double> price_move(0.0, 0.0005); // 0.05% volatility
  double price_change = price_move(rng_);
  state.last_price *= (1.0 + price_change);

  // Keep price within reasonable bounds
  state.last_price = std::max(50.0, std::min(1000.0, state.last_price));

  // Generate trade size
  std::uniform_int_distribution<uint32_t> size_dist(100, 10000);
  state.last_size = size_dist(rng_);

  // Fill trade fields
  msg.trade.price = state.last_price;
  msg.trade.size = state.last_size;
  msg.trade.flags = 0;

  // Zero out padding
  std::memset(msg.padding, 0, sizeof(msg.padding));

  return msg;
}

bool MarketDataSimulator::send_message(const MarketMessage &msg) {
  try {
    // Store message first
    {
      MessageLog::accessor acc;
      message_log_.insert(acc, msg.sequence_number);
      acc->second = msg;
    }

    // Send message
    boost::system::error_code ec;
    auto bytes_sent = socket_.send_to(boost::asio::buffer(&msg, sizeof(msg)),
                                      multicast_endpoint_, 0, ec);

    if (ec || bytes_sent != sizeof(msg)) {
      fmt::print(stderr, "Error sending message {}: {}\n", msg.sequence_number,
                 ec ? ec.message() : "Incomplete send");
      return false;
    }

    const auto sent = ++messages_sent_;
    if (sent % 1000 == 0) {
      fmt::print("Successfully sent {} messages\n", sent);
    }
    return true;

  } catch (const std::exception &e) {
    fmt::print(stderr, "Exception in send_message: {}\n", e.what());
    return false;
  }
}

MarketDataSimulator::Stats MarketDataSimulator::get_stats() const {
  Stats stats;
  stats.messages_sent = messages_sent_;
  stats.current_rate = current_rate_;
  stats.messages_dropped = messages_dropped_;
  return stats;
}

} // namespace tick_capture::benchmark
