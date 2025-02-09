#include "capture_node.hpp"
#include <fmt/format.h>

namespace tick_capture {

CaptureNode::CaptureNode(const CaptureConfig &config)
    : config_(config), capture_(std::make_unique<PacketCapture>(config)),
      storage_(std::make_unique<TickStorage>(config.output_dir)) {

  // Only create coordinator if we're in distributed mode
  if (!config.coordinator_address.empty()) {
    coordinator_ = std::make_unique<Coordinator>(config.coordinator_address,
                                                 config.peer_addresses);
  }
}

CaptureNode::~CaptureNode() { stop(); }

void CaptureNode::start() {
  if (running_)
    return;
  running_ = true;

  // Start capture
  capture_->start();

  // Start coordinator if in distributed mode
  if (coordinator_) {
    coordinator_->start();
  }

  // Start processing thread
  process_thread_ = std::thread([this] { process_messages(); });

  // Start stats reporting thread
  stats_thread_ = std::thread([this] { report_stats(); });
}

void CaptureNode::stop() {
  if (!running_)
    return;
  running_ = false;

  capture_->stop();
  if (coordinator_) {
    coordinator_->stop();
  }

  if (process_thread_.joinable())
    process_thread_.join();
  if (stats_thread_.joinable())
    stats_thread_.join();

  // Flush storage
  storage_->flush();
}

void CaptureNode::process_messages() {
  constexpr size_t batch_size = 32;
  std::vector<MarketMessage> batch;
  batch.reserve(batch_size);

  while (running_) {
    auto &buffer = capture_->get_buffer();

    // Process messages in batches
    const size_t processed =
        buffer.pop_bulk(std::back_inserter(batch), batch_size);

    if (processed > 0) {
      // Process each message in the batch
      for (const auto &msg : batch) {
        // Check for sequence gaps
        uint64_t last_seq = last_sequence_.load();
        if (last_seq > 0 && msg.sequence_number > last_seq + 1) {
          fmt::print("Sequence gap: {} -> {}\n", last_seq, msg.sequence_number);
        }
        last_sequence_.store(msg.sequence_number);

        // Store the message
        storage_->store(msg);
        messages_processed_.fetch_add(1, std::memory_order_relaxed);
      }

      batch.clear();
    }

    // Small sleep if no messages to prevent busy-waiting
    if (processed == 0) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
}

void CaptureNode::report_stats() {
  using namespace std::chrono;
  auto next_report = system_clock::now();

  while (running_) {
    auto stats = get_stats();

    // Print local stats
    fmt::print(
        "Messages - Received: {} Processed: {} Dropped: {} Rate: {:.2f}k/s\n",
        stats.messages_received, stats.messages_processed,
        stats.messages_dropped,
        static_cast<double>(stats.messages_processed) / 1000.0);

    // Report to coordinator if in distributed mode
    if (coordinator_) {
      std::string status = fmt::format(
          R"({{"type":"status","stats":{{"received":{},"processed":{},"dropped":{}}}}})",
          stats.messages_received, stats.messages_processed,
          stats.messages_dropped);
      coordinator_->publish_status(status);
    }

    // Schedule next report
    next_report += seconds(1);
    std::this_thread::sleep_until(next_report);
  }
}

CaptureStats CaptureNode::get_stats() const {
  auto capture_stats = capture_->get_stats();
  capture_stats.messages_processed = messages_processed_.load();
  return capture_stats;
}

} // namespace tick_capture
