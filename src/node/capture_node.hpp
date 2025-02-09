#pragma once
#include "../../include/tick_capture/types.hpp"
#include "../capture/packet_capture.hpp"
#include "../network/coordinator.hpp"
#include "../storage/tick_storage.hpp"

namespace tick_capture {

class CaptureNode {
public:
  explicit CaptureNode(const CaptureConfig &config);
  ~CaptureNode();

  // Start/stop the node
  void start();
  void stop();

  // Get node statistics
  CaptureStats get_stats() const;

private:
  void process_messages();
  void report_stats();

  CaptureConfig config_;
  std::unique_ptr<PacketCapture> capture_;
  std::unique_ptr<TickStorage> storage_;
  std::unique_ptr<Coordinator> coordinator_;

  // Processing thread
  std::atomic<bool> running_{false};
  std::thread process_thread_;
  std::thread stats_thread_;

  // Statistics
  std::atomic<uint64_t> messages_processed_{0};
  std::atomic<uint64_t> last_sequence_{0}; // For gap detection
};

} // namespace tick_capture
