#include "../src/node/capture_node.hpp"
#include "market_data_simulator.hpp"
#include <boost/program_options.hpp>
#include <chrono>
#include <fmt/format.h>
#include <fstream>
#include <iostream>

namespace po = boost::program_options;
using namespace tick_capture;
using namespace tick_capture::benchmark;

struct BenchmarkResult {
  uint32_t target_rate;
  uint64_t messages_sent;
  uint64_t messages_captured;
  double capture_rate;
  double avg_latency_ns;
  uint64_t dropped_messages;
  std::chrono::microseconds run_time;
};

class BenchmarkRunner {
public:
  // struct Config {
  //   std::string output_dir;
  //   std::vector<uint32_t> rates = {100000, 250000, 500000, 1000000};
  //   std::chrono::seconds duration{60};
  //   bool measure_latency = false;
  //   bool verify_messages = true;
  // };
  // struct Config {
  //  std::string output_dir;
  //  // Start with much lower rates for testing
  //  std::vector<uint32_t> rates = {1000, 5000, 10000};
  //  std::chrono::seconds duration{10}; // Shorter duration for testing
  //  bool measure_latency = false;
  //  bool verify_messages = true;
  //  bool verbose_logging = true; // Add verbose logging option
  //};

  struct Config {
    std::string output_dir;
    // Start with very low rates for initial testing
    std::vector<uint32_t> rates = {
        10,  // 10 msgs/sec - baseline
        50,  // 50 msgs/sec
        100, // 100 msgs/sec
        200, // 200 msgs/sec
        500  // 500 msgs/sec
    };
    std::chrono::seconds duration{5}; // Shorter duration for quick testing
    bool measure_latency = false;
    bool verify_messages = true;
    bool verbose_logging = true;
  };

  explicit BenchmarkRunner(const Config &config) : config_(config) {
    std::filesystem::create_directories(config.output_dir);
  }

  BenchmarkResult run_benchmark(uint32_t target_rate) {
    fmt::print("\nStarting benchmark at {} msgs/sec for {} seconds\n",
               target_rate, config_.duration.count());

    // Setup simulator config
    MarketDataSimulator::Config sim_config;
    sim_config.base_msg_rate = target_rate;
    sim_config.num_symbols = 10;
    sim_config.burst_size = 0;

    // Setup capture config
    CaptureConfig capture_config;
    capture_config.output_dir =
        fmt::format("{}/bench_{}", config_.output_dir, target_rate);
    capture_config.enable_timestamps = config_.measure_latency;

    // Create components
    auto simulator = std::make_unique<MarketDataSimulator>(sim_config);
    auto capture_node = std::make_unique<CaptureNode>(capture_config);

    // Start capture first
    capture_node->start();

    // Brief delay to ensure capture is ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Start simulation and timing
    const auto start_time = std::chrono::high_resolution_clock::now();
    simulator->start();

    // Run for specified duration
    std::this_thread::sleep_for(config_.duration);

    // Stop components in reverse order
    simulator->stop();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(100)); // Allow time for last messages
    capture_node->stop();

    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto run_time = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time);

    // Calculate results
    BenchmarkResult result;
    result.target_rate = target_rate;
    result.messages_sent = simulator->get_stats().messages_sent;

    auto capture_stats = capture_node->get_stats();
    result.messages_captured = capture_stats.messages_processed;
    result.dropped_messages = capture_stats.messages_dropped;

    result.capture_rate = static_cast<double>(result.messages_captured) /
                          static_cast<double>(result.messages_sent) * 100.0;

    result.run_time = run_time;

    // Verify captured messages if enabled
    if (config_.verify_messages) {
      verify_capture(simulator->get_message_log(), capture_config.output_dir,
                     sim_config);
    }

    return result;
  }

  void print_results(const BenchmarkResult &result) {
    fmt::print("\nBenchmark Results:\n");
    fmt::print("================\n");
    fmt::print("Target Rate: {} msgs/sec\n", result.target_rate);
    fmt::print("Messages Sent: {}\n", result.messages_sent);
    fmt::print("Messages Captured: {}\n", result.messages_captured);
    fmt::print("Messages Dropped: {}\n", result.dropped_messages);
    fmt::print("Capture Rate: {:.2f}%\n", result.capture_rate);
    fmt::print("Run Time: {:.2f} seconds\n",
               static_cast<double>(result.run_time.count()) / 1'000'000);

    if (result.avg_latency_ns > 0) {
      fmt::print("Average Latency: {:.2f} ns\n", result.avg_latency_ns);
    }
  }

  void log_message_sample(const MarketMessage &msg, const char *prefix) {
    fmt::print("{}: seq={}, sym={}, type={}, price={:.2f}, size={}\n", prefix,
               msg.sequence_number, msg.symbol_id, static_cast<int>(msg.type),
               msg.trade.price, msg.trade.size);
  }

private:
  bool is_valid_tick_file(const std::filesystem::path &path) {
    try {
      // Extract symbol id from filename
      auto stem = path.stem().string();
      auto symbol_id = std::stoul(stem);
      return symbol_id > 0 && symbol_id <= 10000;
    } catch (...) {
      return false;
    }
  }

  void verify_capture(const MarketDataSimulator::MessageLog &sent_messages,
                      const std::string &capture_dir,
                      const MarketDataSimulator::Config
                          &sim_config) { // Added sim_config parameter

    fmt::print("\nStarting message verification...\n");
    fmt::print("Verifying files in: {}\n", capture_dir);

    struct Stats {
      uint64_t total_read = 0;
      uint64_t valid_messages = 0;
      uint64_t invalid_messages = 0;
      uint64_t mismatches = 0;
      uint64_t missing_sent = 0;
      uint64_t min_seq = UINT64_MAX;
      uint64_t max_seq = 0;
    } stats;

    // List all files first
    std::vector<std::filesystem::path> tick_files;
    for (const auto &entry : std::filesystem::directory_iterator(capture_dir)) {
      if (entry.path().extension() == ".tick" &&
          is_valid_tick_file(entry.path())) {
        tick_files.push_back(entry.path());
        fmt::print("Found valid tick file: {}\n", entry.path().string());
      }
    }

    // First pass - validate basic message structure
    for (const auto &file_path : tick_files) {
      std::ifstream file(file_path, std::ios::binary);
      MarketMessage msg;
      size_t file_messages = 0;

      fmt::print("Processing file: {}\n", file_path.string());

      while (file.read(reinterpret_cast<char *>(&msg), sizeof(msg))) {
        stats.total_read++;
        file_messages++;

        // Debug first few messages from each file
        if (file_messages <= 5) {
          fmt::print("Read message {}: seq={}, sym={}, price={:.2f}, size={}\n",
                     file_messages, msg.sequence_number, msg.symbol_id,
                     msg.trade.price, msg.trade.size);
        }

        if (msg.sequence_number > 0 && msg.symbol_id > 0 &&
            msg.symbol_id <= 10000 && msg.type == MessageType::Trade &&
            msg.trade.price > 0) {

          stats.valid_messages++;
          stats.min_seq = std::min(stats.min_seq, msg.sequence_number);
          stats.max_seq = std::max(stats.max_seq, msg.sequence_number);
        } else {
          stats.invalid_messages++;
          if (stats.invalid_messages < 10) {
            fmt::print("Invalid message in {}: seq={}, sym={}, type={}, "
                       "price={:.2f}\n",
                       file_path.filename().string(), msg.sequence_number,
                       msg.symbol_id, static_cast<int>(msg.type),
                       msg.trade.price);
          }
        }
      }
      fmt::print("Finished file {}. Read {} messages\n", file_path.string(),
                 file_messages);
    }

    fmt::print("\nBasic Statistics:\n");
    fmt::print("  Total messages read: {}\n", stats.total_read);
    fmt::print("  Valid messages: {}\n", stats.valid_messages);
    fmt::print("  Invalid messages: {}\n", stats.invalid_messages);

    if (stats.valid_messages > 0) {
      fmt::print("  Sequence range: {} to {}\n", stats.min_seq, stats.max_seq);

      // Second pass - compare with sent messages
      for (const auto &entry :
           std::filesystem::directory_iterator(capture_dir)) {
        if (entry.path().extension() == ".tick") {
          std::ifstream file(entry.path(), std::ios::binary);
          MarketMessage msg;

          while (file.read(reinterpret_cast<char *>(&msg), sizeof(msg))) {
            if (msg.sequence_number > 0 && msg.symbol_id > 0 &&
                msg.symbol_id <=
                    sim_config.num_symbols && // Using sim_config here
                msg.type == MessageType::Trade &&
                msg.trade.price > 0) {

              MarketDataSimulator::MessageLog::const_accessor acc;
              if (sent_messages.find(acc, msg.sequence_number)) {
                const auto &sent = acc->second;
                if (!compare_messages(msg, sent)) {
                  stats.mismatches++;
                  if (stats.mismatches < 10) {
                    print_message_mismatch(msg, sent);
                  }
                }
              } else {
                stats.missing_sent++;
                if (stats.missing_sent < 10) {
                  fmt::print("Missing sent message: seq={}\n",
                             msg.sequence_number);
                }
              }
            }
          }
        }
      }

      fmt::print("\nVerification Results:\n");
      fmt::print("  Verified messages: {}\n", stats.valid_messages);
      fmt::print("  Mismatches: {}\n", stats.mismatches);
      fmt::print("  Missing sent messages: {}\n", stats.missing_sent);

      if (stats.valid_messages > 0) {
        fmt::print(
            "  Error rate: {:.2f}%\n",
            (static_cast<double>(stats.mismatches) / stats.valid_messages) *
                100.0);
      }
    }
  }

  // Helper function to compare messages
  bool compare_messages(const MarketMessage &a, const MarketMessage &b) {
    return a.sequence_number == b.sequence_number &&
           a.symbol_id == b.symbol_id && a.type == b.type &&
           std::abs(a.trade.price - b.trade.price) <
               0.001 && // Allow small floating point differences
           a.trade.size == b.trade.size;
  }

  // Helper function to print mismatches
  void print_message_mismatch(const MarketMessage &captured,
                              const MarketMessage &sent) {
    fmt::print("Mismatch at {}: ", captured.sequence_number);
    if (captured.symbol_id != sent.symbol_id)
      fmt::print("sym:{}->{} ", captured.symbol_id, sent.symbol_id);
    if (captured.type != sent.type)
      fmt::print("type:{}->{} ", static_cast<int>(captured.type),
                 static_cast<int>(sent.type));
    if (std::abs(captured.trade.price - sent.trade.price) >= 0.001)
      fmt::print("price:{:.2f}->{:.2f} ", captured.trade.price,
                 sent.trade.price);
    if (captured.trade.size != sent.trade.size)
      fmt::print("size:{}->{}", captured.trade.size, sent.trade.size);
    fmt::print("\n");
  }
  Config config_;
};

int main(int argc, char *argv[]) {
  po::options_description desc("Allowed options");
  desc.add_options()("help", "produce help message")(
      "output-dir", po::value<std::string>()->default_value("/tmp/tick_bench"),
      "output directory for captured data")(
      "duration", po::value<uint32_t>()->default_value(60),
      "benchmark duration in seconds")("latency",
                                       po::bool_switch()->default_value(false),
                                       "enable latency measurements")(
      "verify", po::bool_switch()->default_value(true),
      "verify captured messages")(
      "rate", po::value<std::vector<uint32_t>>()->multitoken(),
      "custom message rates to test");

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 1;
  }

  // Setup benchmark config
  BenchmarkRunner::Config config;
  config.output_dir = vm["output-dir"].as<std::string>();
  config.duration = std::chrono::seconds(vm["duration"].as<uint32_t>());
  config.measure_latency = vm["latency"].as<bool>();
  config.verify_messages = vm["verify"].as<bool>();

  if (vm.count("rate")) {
    config.rates = vm["rate"].as<std::vector<uint32_t>>();
  }

  // Run benchmarks
  BenchmarkRunner runner(config);
  bool exceeded_drop_threshold = false;

  for (auto rate : config.rates) {
    auto result = runner.run_benchmark(rate);
    runner.print_results(result);

    // Stop if drop rate exceeds threshold
    if (result.capture_rate < 99.0) {
      fmt::print("\nCapture rate dropped below 99% - stopping benchmark\n");
      exceeded_drop_threshold = true;
      break;
    }

    // Brief pause between runs
    std::this_thread::sleep_for(std::chrono::seconds(5));
  }

  return exceeded_drop_threshold ? 1 : 0;
}
